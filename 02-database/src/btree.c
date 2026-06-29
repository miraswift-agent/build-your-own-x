/*
 * btree.c — B-tree storage engine implementation
 *
 * Page-based B-tree with configurable order (min degree t).
 * Page 0 = header, Page 1 = free list, Page 2+ = nodes.
 * Each node occupies exactly one 4096-byte page.
 * Values that don't fit in a node cell spill to overflow pages.
 */

#include "btree.h"
#include "btree_internal.h"
#include "pager.h"
#include "cursor.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================
 * Constants and limits (supplementing btree_internal.h)
 * ============================================================ */

#define MAX_CELL_SIZE    (MAX_INLINE_KEY + MAX_INLINE_VAL)
#define HEADER_SIZE      4
#define PTR_SIZE         4
#define LEN_SIZE         2
#define OVERFLOW_PTR_SIZE 4

/* ============================================================
 * Overflow page management
 * ============================================================ */

/* Overflow page layout:
 *   [next_page:4][data_len:2][data:4090]
 * Values larger than MAX_INLINE_VAL chain across multiple overflow pages.
 */

typedef struct {
    uint32_t next_page;
    uint16_t data_len;
    uint8_t  data[PAGE_SIZE - 6];
} OverflowPage;

/* Write a value to overflow pages, return the first page number.
 * Returns 0 on error. */
static uint32_t overflow_write(BTREE *tree, const void *data, size_t len)
{
    if (len == 0) return 0;

    const uint8_t *src = (const uint8_t *)data;
    size_t remaining = len;
    uint32_t first_page = 0;
    uint32_t prev_page = 0;
    uint32_t max_data = PAGE_SIZE - 6;

    while (remaining > 0) {
        uint32_t pg = pager_allocate(&tree->pager);
        if (pg == 0) return 0;

        uint8_t buf[PAGE_SIZE];
        memset(buf, 0, PAGE_SIZE);
        OverflowPage *op = (OverflowPage *)buf;

        size_t chunk = remaining > max_data ? max_data : remaining;
        op->data_len = (uint16_t)chunk;
        memcpy(op->data, src, chunk);
        op->next_page = 0;

        if (prev_page != 0) {
            uint8_t prev_buf[PAGE_SIZE];
            if (pager_read_page(&tree->pager, prev_page, prev_buf) != 0) return 0;
            OverflowPage *prev_op = (OverflowPage *)prev_buf;
            prev_op->next_page = pg;
            if (pager_write_page(&tree->pager, prev_page, prev_buf) != 0) return 0;
        } else {
            first_page = pg;
        }

        if (pager_write_page(&tree->pager, pg, buf) != 0) return 0;

        src += chunk;
        remaining -= chunk;
        prev_page = pg;
    }

    return first_page;
}

/* Read a value from overflow pages starting at `page_num`.
 * Allocates and returns a malloc'd buffer. Caller must free. */
int overflow_read(BTREE *tree, uint32_t page_num, void **out, size_t *out_len)
{
    size_t total = 0;
    uint32_t pg = page_num;
    while (pg != 0) {
        uint8_t buf[PAGE_SIZE];
        if (pager_read_page(&tree->pager, pg, buf) != 0) return BTREE_ERR;
        OverflowPage *op = (OverflowPage *)buf;
        total += op->data_len;
        pg = op->next_page;
    }

    uint8_t *result = (uint8_t *)malloc(total);
    if (!result) return BTREE_ERR;

    size_t offset = 0;
    pg = page_num;
    while (pg != 0) {
        uint8_t buf[PAGE_SIZE];
        if (pager_read_page(&tree->pager, pg, buf) != 0) { free(result); return BTREE_ERR; }
        OverflowPage *op = (OverflowPage *)buf;
        memcpy(result + offset, op->data, op->data_len);
        offset += op->data_len;
        pg = op->next_page;
    }

    *out = result;
    *out_len = total;
    return BTREE_OK;
}

/* Free overflow page chain */
static void overflow_free(BTREE *tree, uint32_t page_num)
{
    while (page_num != 0) {
        uint8_t buf[PAGE_SIZE];
        if (pager_read_page(&tree->pager, page_num, buf) != 0) return;
        OverflowPage *op = (OverflowPage *)buf;
        uint32_t next = op->next_page;
        pager_free(&tree->pager, page_num);
        page_num = next;
    }
}

/* ============================================================
 * Node serialization
 * ============================================================ */

static int node_serialize(Node *node, uint8_t *buf)
{
    memset(buf, 0, PAGE_SIZE);
    size_t off = 0;

    /* Header */
    buf[off++] = node->flag;
    buf[off++] = (node->nkeys >> 8) & 0xFF;
    buf[off++] = node->nkeys & 0xFF;
    buf[off++] = 0;

    /* Key lengths */
    for (int i = 0; i < node->nkeys; i++) {
        buf[off++] = (node->cells[i].key_len >> 8) & 0xFF;
        buf[off++] = node->cells[i].key_len & 0xFF;
    }

    /* Value lengths */
    for (int i = 0; i < node->nkeys; i++) {
        buf[off++] = (node->cells[i].val_len >> 8) & 0xFF;
        buf[off++] = node->cells[i].val_len & 0xFF;
    }

    /* Overflow flags */
    for (int i = 0; i < node->nkeys; i++) {
        buf[off++] = node->cells[i].is_overflow;
    }

    /* Child page numbers (for internal nodes) */
    if (!(node->flag & NODE_FLAG_LEAF)) {
        for (int i = 0; i <= node->nkeys; i++) {
            buf[off++] = (node->children[i] >> 24) & 0xFF;
            buf[off++] = (node->children[i] >> 16) & 0xFF;
            buf[off++] = (node->children[i] >> 8) & 0xFF;
            buf[off++] = node->children[i] & 0xFF;
        }
    }

    /* Cell data: keys */
    for (int i = 0; i < node->nkeys; i++) {
        memcpy(buf + off, node->cells[i].key, node->cells[i].key_len);
        off += node->cells[i].key_len;
    }

    /* Cell data: values (inline only) */
    for (int i = 0; i < node->nkeys; i++) {
        if (!node->cells[i].is_overflow) {
            size_t vlen = node->cells[i].val_len > MAX_INLINE_VAL ?
                           MAX_INLINE_VAL : node->cells[i].val_len;
            memcpy(buf + off, node->cells[i].val_data.inline_val, vlen);
            off += vlen;
        }
    }

    /* Overflow page pointers */
    for (int i = 0; i < node->nkeys; i++) {
        if (node->cells[i].is_overflow) {
            buf[off++] = (node->cells[i].val_data.overflow_page >> 24) & 0xFF;
            buf[off++] = (node->cells[i].val_data.overflow_page >> 16) & 0xFF;
            buf[off++] = (node->cells[i].val_data.overflow_page >> 8) & 0xFF;
            buf[off++] = node->cells[i].val_data.overflow_page & 0xFF;
        }
    }

    if (off > PAGE_SIZE) {
        fprintf(stderr, "node_serialize: node too large (%zu bytes)\n", off);
        return BTREE_ERR;
    }

    return BTREE_OK;
}

static int node_deserialize(Node *node, const uint8_t *buf, uint32_t page_num)
{
    memset(node, 0, sizeof(*node));
    node->page_num = page_num;
    size_t off = 0;

    node->flag = buf[off++];
    node->nkeys = ((uint16_t)buf[off] << 8) | buf[off + 1];
    off += 2;
    off++; /* padding */

    if (node->nkeys > 254) {
        fprintf(stderr, "node_deserialize: too many keys (%d) in page %u\n",
                node->nkeys, page_num);
        return BTREE_ERR;
    }

    for (int i = 0; i < node->nkeys; i++) {
        node->cells[i].key_len = ((uint16_t)buf[off] << 8) | buf[off + 1];
        off += 2;
    }

    for (int i = 0; i < node->nkeys; i++) {
        node->cells[i].val_len = ((uint16_t)buf[off] << 8) | buf[off + 1];
        off += 2;
    }

    for (int i = 0; i < node->nkeys; i++) {
        node->cells[i].is_overflow = buf[off++];
    }

    if (!(node->flag & NODE_FLAG_LEAF)) {
        for (int i = 0; i <= node->nkeys; i++) {
            node->children[i] = ((uint32_t)buf[off] << 24) |
                                 ((uint32_t)buf[off+1] << 16) |
                                 ((uint32_t)buf[off+2] << 8) |
                                 buf[off+3];
            off += 4;
        }
    }

    for (int i = 0; i < node->nkeys; i++) {
        if (node->cells[i].key_len > MAX_INLINE_KEY) {
            fprintf(stderr, "node_deserialize: key too large (%d)\n", node->cells[i].key_len);
            return BTREE_ERR;
        }
        memcpy(node->cells[i].key, buf + off, node->cells[i].key_len);
        off += node->cells[i].key_len;
    }

    for (int i = 0; i < node->nkeys; i++) {
        if (!node->cells[i].is_overflow) {
            size_t vlen = node->cells[i].val_len > MAX_INLINE_VAL ?
                           MAX_INLINE_VAL : node->cells[i].val_len;
            memcpy(node->cells[i].val_data.inline_val, buf + off, vlen);
            off += vlen;
        }
    }

    for (int i = 0; i < node->nkeys; i++) {
        if (node->cells[i].is_overflow) {
            node->cells[i].val_data.overflow_page =
                ((uint32_t)buf[off] << 24) |
                ((uint32_t)buf[off+1] << 16) |
                ((uint32_t)buf[off+2] << 8) |
                buf[off+3];
            off += 4;
        }
    }

    return BTREE_OK;
}

/* ============================================================
 * Node read/write helpers
 * ============================================================ */

int node_read(BTREE *tree, uint32_t page_num, Node *node)
{
    uint8_t buf[PAGE_SIZE];
    if (pager_read_page(&tree->pager, page_num, buf) != 0) return BTREE_ERR;
    return node_deserialize(node, buf, page_num);
}

int node_write(BTREE *tree, Node *node)
{
    uint8_t buf[PAGE_SIZE];
    if (node_serialize(node, buf) != BTREE_OK) return BTREE_ERR;
    return pager_write_page(&tree->pager, node->page_num, buf);
}

/* ============================================================
 * Key comparison
 * ============================================================ */

static int key_compare(const void *k1, size_t k1_len, const void *k2, size_t k2_len)
{
    size_t min_len = k1_len < k2_len ? k1_len : k2_len;
    int cmp = memcmp(k1, k2, min_len);
    if (cmp != 0) return cmp;
    if (k1_len < k2_len) return -1;
    if (k1_len > k2_len) return 1;
    return 0;
}

/* ============================================================
 * B-tree operations
 * ============================================================ */

BTREE* btree_open(const char *filename, int order)
{
    if (order < 2) {
        fprintf(stderr, "btree_open: order must be >= 2 (got %d)\n", order);
        return NULL;
    }

    BTREE *tree = (BTREE *)calloc(1, sizeof(BTREE));
    if (!tree) return NULL;

    tree->order = order;
    tree->max_keys = 2 * order - 1;
    tree->min_keys = order - 1;
    tree->max_cells = tree->max_keys;
    if (tree->max_cells > 127) tree->max_cells = 127;

    if (pager_open(&tree->pager, filename) != 0) {
        free(tree);
        return NULL;
    }

    uint8_t hdr_buf[PAGE_SIZE];
    if (pager_read_page(&tree->pager, 0, hdr_buf) != 0) {
        pager_close(&tree->pager);
        free(tree);
        return NULL;
    }

    HeaderPage *hdr = (HeaderPage *)hdr_buf;

    if (hdr->magic == MAGIC_NUMBER) {
        tree->root_page = hdr->root_page;
    } else {
        uint32_t root = pager_allocate(&tree->pager);
        if (root == 0) {
            pager_close(&tree->pager);
            free(tree);
            return NULL;
        }

        Node root_node;
        memset(&root_node, 0, sizeof(root_node));
        root_node.page_num = root;
        root_node.flag = NODE_FLAG_LEAF;
        root_node.nkeys = 0;

        if (node_write(tree, &root_node) != BTREE_OK) {
            pager_close(&tree->pager);
            free(tree);
            return NULL;
        }

        tree->root_page = root;

        if (pager_write_header(&tree->pager, (uint32_t)order, root) != 0) {
            pager_close(&tree->pager);
            free(tree);
            return NULL;
        }
    }

    return tree;
}

int btree_close(BTREE *tree)
{
    if (!tree) return BTREE_ERR;
    pager_write_header(&tree->pager, (uint32_t)tree->order, tree->root_page);
    pager_close(&tree->pager);
    free(tree);
    return BTREE_OK;
}

/* ============================================================
 * Search
 * ============================================================ */

int btree_search(BTREE *tree, const void *key, size_t key_len,
                 void **value, size_t *value_len)
{
    if (!tree || !key) return BTREE_ERR;
    tree->n_searches++;

    uint32_t page = tree->root_page;

    while (page != 0) {
        Node node;
        if (node_read(tree, page, &node) != BTREE_OK) return BTREE_ERR;

        int lo = 0, hi = node.nkeys - 1, found = -1;
        while (lo <= hi) {
            int mid = lo + (hi - lo) / 2;
            int cmp = key_compare(key, key_len, node.cells[mid].key, node.cells[mid].key_len);
            if (cmp == 0) { found = mid; break; }
            else if (cmp < 0) hi = mid - 1;
            else lo = mid + 1;
        }

        if (found >= 0) {
            if (node.cells[found].is_overflow) {
                return overflow_read(tree, node.cells[found].val_data.overflow_page,
                                     value, value_len);
            } else {
                *value_len = node.cells[found].val_len;
                *value = malloc(*value_len);
                if (!*value) return BTREE_ERR;
                memcpy(*value, node.cells[found].val_data.inline_val, *value_len);
                return BTREE_OK;
            }
        }

        if (node.flag & NODE_FLAG_LEAF) return BTREE_NOTFOUND;
        page = node.children[lo];
    }

    return BTREE_NOTFOUND;
}

/* ============================================================
 * Insert
 * ============================================================ */

static int btree_split_child(BTREE *tree, Node *parent, int split_index)
{
    Node child;
    if (node_read(tree, parent->children[split_index], &child) != BTREE_OK)
        return BTREE_ERR;

    uint32_t new_page = pager_allocate(&tree->pager);
    if (new_page == 0) return BTREE_ERR;

    Node new_node;
    memset(&new_node, 0, sizeof(new_node));
    new_node.page_num = new_page;
    new_node.flag = child.flag;

    int t = tree->order;

    new_node.nkeys = (uint16_t)(t - 1);
    for (int i = 0; i < t - 1; i++) {
        new_node.cells[i] = child.cells[t + i];
    }

    if (!(child.flag & NODE_FLAG_LEAF)) {
        for (int i = 0; i < t; i++) {
            new_node.children[i] = child.children[t + i];
        }
    }

    Cell median = child.cells[t - 1];

    for (int i = parent->nkeys; i > split_index; i--) {
        parent->cells[i] = parent->cells[i - 1];
    }
    parent->cells[split_index] = median;

    for (int i = parent->nkeys + 1; i > split_index + 1; i--) {
        parent->children[i] = parent->children[i - 1];
    }
    parent->children[split_index + 1] = new_page;
    parent->nkeys++;

    child.nkeys = (uint16_t)(t - 1);

    if (node_write(tree, &child) != BTREE_OK) return BTREE_ERR;
    if (node_write(tree, &new_node) != BTREE_OK) return BTREE_ERR;
    if (node_write(tree, parent) != BTREE_OK) return BTREE_ERR;

    return BTREE_OK;
}

static int btree_insert_nonfull(BTREE *tree, Node *node,
                                const void *key, size_t key_len,
                                const void *value, size_t value_len)
{
    if (node->flag & NODE_FLAG_LEAF) {
        int pos = node->nkeys;
        for (int i = 0; i < node->nkeys; i++) {
            int cmp = key_compare(key, key_len, node->cells[i].key, node->cells[i].key_len);
            if (cmp == 0) return BTREE_EXISTS;
            if (cmp < 0) { pos = i; break; }
        }

        for (int i = node->nkeys; i > pos; i--) {
            node->cells[i] = node->cells[i - 1];
        }

        node->cells[pos].key_len = (uint16_t)key_len;
        node->cells[pos].val_len = (uint16_t)value_len;
        memcpy(node->cells[pos].key, key, key_len);

        if (value_len > MAX_INLINE_VAL) {
            node->cells[pos].is_overflow = 1;
            node->cells[pos].val_data.overflow_page =
                overflow_write(tree, value, value_len);
            if (node->cells[pos].val_data.overflow_page == 0) return BTREE_ERR;
        } else {
            node->cells[pos].is_overflow = 0;
            memcpy(node->cells[pos].val_data.inline_val, value, value_len);
        }

        node->nkeys++;
        if (node_write(tree, node) != BTREE_OK) return BTREE_ERR;
        return BTREE_OK;
    }

    int pos = node->nkeys;
    for (int i = 0; i < node->nkeys; i++) {
        int cmp = key_compare(key, key_len, node->cells[i].key, node->cells[i].key_len);
        if (cmp == 0) return BTREE_EXISTS;
        if (cmp < 0) { pos = i; break; }
    }

    Node child;
    if (node_read(tree, node->children[pos], &child) != BTREE_OK) return BTREE_ERR;

    if (child.nkeys == (uint16_t)tree->max_keys) {
        if (btree_split_child(tree, node, pos) != BTREE_OK) return BTREE_ERR;
        if (node_read(tree, node->page_num, node) != BTREE_OK) return BTREE_ERR;

        int cmp = key_compare(key, key_len, node->cells[pos].key, node->cells[pos].key_len);
        if (cmp > 0) pos++;

        if (node_read(tree, node->children[pos], &child) != BTREE_OK) return BTREE_ERR;
    }

    return btree_insert_nonfull(tree, &child, key, key_len, value, value_len);
}

int btree_insert(BTREE *tree, const void *key, size_t key_len,
                 const void *value, size_t value_len)
{
    if (!tree || !key) return BTREE_ERR;
    if (key_len > MAX_INLINE_KEY) {
        fprintf(stderr, "btree_insert: key too large (%zu > %d)\n",
                key_len, MAX_INLINE_KEY);
        return BTREE_ERR;
    }
    if (key_len == 0 || value_len == 0) return BTREE_ERR;

    tree->n_inserts++;

    Node root;
    if (node_read(tree, tree->root_page, &root) != BTREE_OK) return BTREE_ERR;

    if (root.nkeys == (uint16_t)tree->max_keys) {
        uint32_t new_root_page = pager_allocate(&tree->pager);
        if (new_root_page == 0) return BTREE_ERR;

        Node new_root;
        memset(&new_root, 0, sizeof(new_root));
        new_root.page_num = new_root_page;
        new_root.flag = 0;
        new_root.nkeys = 0;
        new_root.children[0] = tree->root_page;

        if (node_write(tree, &new_root) != BTREE_OK) return BTREE_ERR;

        if (btree_split_child(tree, &new_root, 0) != BTREE_OK) return BTREE_ERR;

        tree->root_page = new_root_page;
        if (node_read(tree, tree->root_page, &root) != BTREE_OK) return BTREE_ERR;
    }

    return btree_insert_nonfull(tree, &root, key, key_len, value, value_len);
}

/* ============================================================
 * Delete
 * ============================================================ */

static int find_key_index(Node *node, const void *key, size_t key_len)
{
    int lo = 0, hi = node->nkeys - 1;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        int cmp = key_compare(key, key_len, node->cells[mid].key, node->cells[mid].key_len);
        if (cmp == 0) return mid;
        if (cmp < 0) hi = mid - 1;
        else lo = mid + 1;
    }
    return -1;
}

static int get_predecessor(BTREE *tree, uint32_t page, Cell *result)
{
    Node node;
    if (node_read(tree, page, &node) != BTREE_OK) return BTREE_ERR;
    while (!(node.flag & NODE_FLAG_LEAF)) {
        if (node_read(tree, node.children[node.nkeys], &node) != BTREE_OK) return BTREE_ERR;
    }
    *result = node.cells[node.nkeys - 1];
    return BTREE_OK;
}

static int get_successor(BTREE *tree, uint32_t page, Cell *result)
{
    Node node;
    if (node_read(tree, page, &node) != BTREE_OK) return BTREE_ERR;
    while (!(node.flag & NODE_FLAG_LEAF)) {
        if (node_read(tree, node.children[0], &node) != BTREE_OK) return BTREE_ERR;
    }
    *result = node.cells[0];
    return BTREE_OK;
}

static int merge_children(BTREE *tree, Node *node, int idx)
{
    Node left, right;
    if (node_read(tree, node->children[idx], &left) != BTREE_OK) return BTREE_ERR;
    if (node_read(tree, node->children[idx + 1], &right) != BTREE_OK) return BTREE_ERR;

    left.cells[left.nkeys] = node->cells[idx];
    left.nkeys++;

    for (int i = 0; i < right.nkeys; i++) {
        left.cells[left.nkeys + i] = right.cells[i];
    }

    if (!(left.flag & NODE_FLAG_LEAF)) {
        for (int i = 0; i <= right.nkeys; i++) {
            left.children[left.nkeys + i] = right.children[i];
        }
    }

    left.nkeys += right.nkeys;

    for (int i = idx; i < node->nkeys - 1; i++) {
        node->cells[i] = node->cells[i + 1];
    }
    for (int i = idx + 1; i < node->nkeys; i++) {
        node->children[i] = node->children[i + 1];
    }
    node->nkeys--;

    pager_free(&tree->pager, right.page_num);

    if (node_write(tree, &left) != BTREE_OK) return BTREE_ERR;
    if (node_write(tree, node) != BTREE_OK) return BTREE_ERR;

    return BTREE_OK;
}

static int borrow_from_left(BTREE *tree, Node *parent, int idx)
{
    Node child, left_sib;
    if (node_read(tree, parent->children[idx], &child) != BTREE_OK) return BTREE_ERR;
    if (node_read(tree, parent->children[idx - 1], &left_sib) != BTREE_OK) return BTREE_ERR;

    for (int i = child.nkeys; i > 0; i--) {
        child.cells[i] = child.cells[i - 1];
    }
    if (!(child.flag & NODE_FLAG_LEAF)) {
        for (int i = child.nkeys + 1; i > 0; i--) {
            child.children[i] = child.children[i - 1];
        }
    }

    child.cells[0] = parent->cells[idx - 1];
    child.nkeys++;

    parent->cells[idx - 1] = left_sib.cells[left_sib.nkeys - 1];

    if (!(child.flag & NODE_FLAG_LEAF)) {
        child.children[0] = left_sib.children[left_sib.nkeys];
    }

    left_sib.nkeys--;

    if (node_write(tree, &child) != BTREE_OK) return BTREE_ERR;
    if (node_write(tree, &left_sib) != BTREE_OK) return BTREE_ERR;
    if (node_write(tree, parent) != BTREE_OK) return BTREE_ERR;

    return BTREE_OK;
}

static int borrow_from_right(BTREE *tree, Node *parent, int idx)
{
    Node child, right_sib;
    if (node_read(tree, parent->children[idx], &child) != BTREE_ERR) {} else return BTREE_ERR;
    if (node_read(tree, parent->children[idx + 1], &right_sib) != BTREE_OK) return BTREE_ERR;

    child.cells[child.nkeys] = parent->cells[idx];
    child.nkeys++;

    parent->cells[idx] = right_sib.cells[0];

    if (!(child.flag & NODE_FLAG_LEAF)) {
        child.children[child.nkeys] = right_sib.children[0];
    }

    for (int i = 0; i < right_sib.nkeys - 1; i++) {
        right_sib.cells[i] = right_sib.cells[i + 1];
    }
    if (!(right_sib.flag & NODE_FLAG_LEAF)) {
        for (int i = 0; i < right_sib.nkeys; i++) {
            right_sib.children[i] = right_sib.children[i + 1];
        }
    }
    right_sib.nkeys--;

    if (node_write(tree, &child) != BTREE_OK) return BTREE_ERR;
    if (node_write(tree, &right_sib) != BTREE_OK) return BTREE_ERR;
    if (node_write(tree, parent) != BTREE_OK) return BTREE_ERR;

    return BTREE_OK;
}

static int ensure_min_keys(BTREE *tree, Node *node, int idx)
{
    Node child;
    if (node_read(tree, node->children[idx], &child) != BTREE_OK) return BTREE_ERR;
    if (child.nkeys >= (uint16_t)tree->order) return BTREE_OK;

    int has_left = idx > 0;
    int has_right = idx < node->nkeys;

    if (has_left) {
        Node left_sib;
        if (node_read(tree, node->children[idx - 1], &left_sib) != BTREE_OK) return BTREE_ERR;
        if (left_sib.nkeys > (uint16_t)tree->min_keys) {
            return borrow_from_left(tree, node, idx);
        }
    }

    if (has_right) {
        Node right_sib;
        if (node_read(tree, node->children[idx + 1], &right_sib) != BTREE_OK) return BTREE_ERR;
        if (right_sib.nkeys > (uint16_t)tree->min_keys) {
            return borrow_from_right(tree, node, idx);
        }
    }

    if (has_left) return merge_children(tree, node, idx - 1);
    if (has_right) return merge_children(tree, node, idx);

    return BTREE_ERR;
}

static int btree_delete_recursive(BTREE *tree, Node *node,
                                   const void *key, size_t key_len)
{
    int idx = find_key_index(node, key, key_len);

    if (node->flag & NODE_FLAG_LEAF) {
        if (idx < 0) return BTREE_NOTFOUND;

        if (node->cells[idx].is_overflow) {
            overflow_free(tree, node->cells[idx].val_data.overflow_page);
        }

        for (int i = idx; i < node->nkeys - 1; i++) {
            node->cells[i] = node->cells[i + 1];
        }
        node->nkeys--;

        if (node_write(tree, node) != BTREE_OK) return BTREE_ERR;
        return BTREE_OK;
    }

    if (idx >= 0) {
        Node left_child, right_child;
        if (node_read(tree, node->children[idx], &left_child) != BTREE_OK) return BTREE_ERR;
        if (node_read(tree, node->children[idx + 1], &right_child) != BTREE_OK) return BTREE_ERR;

        if (left_child.nkeys > (uint16_t)tree->min_keys) {
            Cell pred;
            if (get_predecessor(tree, node->children[idx], &pred) != BTREE_OK) return BTREE_ERR;
            node->cells[idx] = pred;
            if (node_write(tree, node) != BTREE_OK) return BTREE_ERR;
            return btree_delete_recursive(tree, &left_child, pred.key, pred.key_len);
        } else if (right_child.nkeys > (uint16_t)tree->min_keys) {
            Cell succ;
            if (get_successor(tree, node->children[idx + 1], &succ) != BTREE_OK) return BTREE_ERR;
            node->cells[idx] = succ;
            if (node_write(tree, node) != BTREE_OK) return BTREE_ERR;
            return btree_delete_recursive(tree, &right_child, succ.key, succ.key_len);
        } else {
            if (merge_children(tree, node, idx) != BTREE_OK) return BTREE_ERR;
            if (node_read(tree, node->page_num, node) != BTREE_OK) return BTREE_ERR;
            if (node->nkeys == 0) {
                tree->root_page = node->children[0];
                pager_free(&tree->pager, node->page_num);
                Node new_root;
                if (node_read(tree, tree->root_page, &new_root) != BTREE_OK) return BTREE_ERR;
                return btree_delete_recursive(tree, &new_root, key, key_len);
            }
            Node child;
            if (node_read(tree, node->children[idx], &child) != BTREE_OK) return BTREE_ERR;
            return btree_delete_recursive(tree, &child, key, key_len);
        }
    }

    int child_idx = 0;
    for (int i = 0; i < node->nkeys; i++) {
        if (key_compare(key, key_len, node->cells[i].key, node->cells[i].key_len) < 0) break;
        child_idx = i + 1;
    }

    if (ensure_min_keys(tree, node, child_idx) != BTREE_OK) return BTREE_ERR;

    if (node_read(tree, node->page_num, node) != BTREE_OK) return BTREE_ERR;

    if (node->nkeys == 0 && node->page_num == tree->root_page) {
        tree->root_page = node->children[0];
        pager_free(&tree->pager, node->page_num);
        Node new_root;
        if (node_read(tree, tree->root_page, &new_root) != BTREE_OK) return BTREE_ERR;
        return btree_delete_recursive(tree, &new_root, key, key_len);
    }

    child_idx = 0;
    for (int i = 0; i < node->nkeys; i++) {
        if (key_compare(key, key_len, node->cells[i].key, node->cells[i].key_len) < 0) break;
        child_idx = i + 1;
    }

    Node child;
    if (node_read(tree, node->children[child_idx], &child) != BTREE_OK) return BTREE_ERR;
    return btree_delete_recursive(tree, &child, key, key_len);
}

int btree_delete(BTREE *tree, const void *key, size_t key_len)
{
    if (!tree || !key) return BTREE_ERR;
    tree->n_deletes++;

    Node root;
    if (node_read(tree, tree->root_page, &root) != BTREE_OK) return BTREE_ERR;

    int result = btree_delete_recursive(tree, &root, key, key_len);

    pager_write_header(&tree->pager, (uint32_t)tree->order, tree->root_page);
    return result;
}

/* ============================================================
 * Stats
 * ============================================================ */

static void count_nodes(BTREE *tree, uint32_t page, int *count, int *leaves, int depth)
{
    if (page == 0) return;
    Node node;
    if (node_read(tree, page, &node) != BTREE_OK) return;
    (*count)++;
    if (node.flag & NODE_FLAG_LEAF) (*leaves)++;

    if (!(node.flag & NODE_FLAG_LEAF)) {
        for (int i = 0; i <= node.nkeys; i++) {
            count_nodes(tree, node.children[i], count, leaves, depth + 1);
        }
    }
}

void btree_stats(BTREE *tree)
{
    if (!tree) return;

    int node_count = 0, leaf_count = 0;
    count_nodes(tree, tree->root_page, &node_count, &leaf_count, 0);

    Node root;
    if (node_read(tree, tree->root_page, &root) != BTREE_OK) return;

    printf("B-tree statistics:\n");
    printf("  Order (min degree t): %d\n", tree->order);
    printf("  Root keys: %d\n", root.nkeys);
    printf("  Total nodes: %d (leaves: %d, internal: %d)\n",
           node_count, leaf_count, node_count - leaf_count);
    printf("  Inserts: %u\n", tree->n_inserts);
    printf("  Deletes: %u\n", tree->n_deletes);
    printf("  Searches: %u\n", tree->n_searches);
    printf("  Pages allocated: %u\n", tree->pager.page_count);
    printf("  Root page: %u\n", tree->root_page);
}