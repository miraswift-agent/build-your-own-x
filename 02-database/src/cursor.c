/*
 * cursor.c — Cursor implementation for in-order B-tree traversal
 *
 * Simple approach: collect all leaf key-value pairs during initialization
 * by doing an in-order traversal, then yield them one by one.
 * This is memory-intensive for large trees but correct and simple.
 *
 * For production: would use on-demand traversal with a path stack.
 * For this implementation: simplicity and correctness first.
 */

#include "btree.h"
#include "btree_internal.h"
#include "pager.h"
#include "cursor.h"
#include <stdlib.h>
#include <string.h>

/* Entry in the cursor's sorted collection */
typedef struct {
    void    *key;
    size_t   key_len;
    void    *value;
    size_t   value_len;
    uint8_t  is_overflow;
    uint32_t overflow_page;
} CursorEntry;

struct CURSOR {
    BTREE          *tree;
    CursorEntry    *entries;
    int             count;
    int             capacity;
    int             position;
    int             done;
};

/* Recursive in-order traversal to collect all entries.
 * In a B-tree (NOT B+tree), keys exist in both internal and leaf nodes.
 * For internal nodes: visit child[i], then key[i], then child[i+1], etc.
 */
static int collect_entries(BTREE *tree, uint32_t page, CursorEntry **entries,
                            int *count, int *capacity)
{
    if (page == 0) return BTREE_OK;

    Node node;
    if (node_read(tree, page, &node) != BTREE_OK) return BTREE_ERR;

    if (node.flag & NODE_FLAG_LEAF) {
        /* Leaf: collect all keys */
        for (int i = 0; i < node.nkeys; i++) {
            if (*count >= *capacity) {
                *capacity = *capacity * 2 + 64;
                *entries = (CursorEntry *)realloc(*entries, *capacity * sizeof(CursorEntry));
                if (!*entries) return BTREE_ERR;
            }

            CursorEntry *e = &(*entries)[*count];
            e->key_len = node.cells[i].key_len;
            e->key = malloc(e->key_len);
            if (!e->key) return BTREE_ERR;
            memcpy(e->key, node.cells[i].key, e->key_len);

            e->is_overflow = node.cells[i].is_overflow;
            e->value_len = node.cells[i].val_len;

            if (e->is_overflow) {
                e->overflow_page = node.cells[i].val_data.overflow_page;
                e->value = NULL; /* loaded on demand */
            } else {
                e->value = malloc(e->value_len);
                if (!e->value) return BTREE_ERR;
                memcpy(e->value, node.cells[i].val_data.inline_val, e->value_len);
            }

            (*count)++;
        }
    } else {
        /* Internal node: visit child[0], key[0], child[1], key[1], ..., child[nkeys] */
        for (int i = 0; i <= node.nkeys; i++) {
            /* Visit left subtree */
            if (collect_entries(tree, node.children[i], entries, count, capacity) != BTREE_OK)
                return BTREE_ERR;

            /* Visit key i (if not last child index) */
            if (i < node.nkeys) {
                if (*count >= *capacity) {
                    *capacity = *capacity * 2 + 64;
                    *entries = (CursorEntry *)realloc(*entries, *capacity * sizeof(CursorEntry));
                    if (!*entries) return BTREE_ERR;
                }

                CursorEntry *e = &(*entries)[*count];
                e->key_len = node.cells[i].key_len;
                e->key = malloc(e->key_len);
                if (!e->key) return BTREE_ERR;
                memcpy(e->key, node.cells[i].key, e->key_len);

                e->is_overflow = node.cells[i].is_overflow;
                e->value_len = node.cells[i].val_len;

                if (e->is_overflow) {
                    e->overflow_page = node.cells[i].val_data.overflow_page;
                    e->value = NULL;
                } else {
                    e->value = malloc(e->value_len);
                    if (!e->value) return BTREE_ERR;
                    memcpy(e->value, node.cells[i].val_data.inline_val, e->value_len);
                }

                (*count)++;
            }
        }
    }

    return BTREE_OK;
}

CURSOR* btree_cursor(BTREE *tree)
{
    if (!tree) return NULL;

    CURSOR *c = (CURSOR *)calloc(1, sizeof(CURSOR));
    if (!c) return NULL;

    c->tree = tree;
    c->position = 0;
    c->done = 0;
    c->capacity = 64;
    c->count = 0;
    c->entries = (CursorEntry *)malloc(c->capacity * sizeof(CursorEntry));
    if (!c->entries) {
        free(c);
        return NULL;
    }

    /* Collect all entries via in-order traversal */
    if (collect_entries(tree, tree->root_page, &c->entries, &c->count, &c->capacity) != BTREE_OK) {
        /* Clean up on failure */
        for (int i = 0; i < c->count; i++) {
            free(c->entries[i].key);
            free(c->entries[i].value);
        }
        free(c->entries);
        free(c);
        return NULL;
    }

    return c;
}

int cursor_next(CURSOR *c, void **key, size_t *key_len,
                void **value, size_t *value_len)
{
    if (!c || !key || !value) return BTREE_ERR;
    if (c->done || c->position >= c->count) return BTREE_NOTFOUND;

    CursorEntry *e = &c->entries[c->position];

    /* Copy key */
    *key_len = e->key_len;
    *key = malloc(*key_len);
    if (!*key) return BTREE_ERR;
    memcpy(*key, e->key, *key_len);

    /* Copy value */
    if (e->is_overflow) {
        /* Load value from overflow pages */
        int rc = overflow_read(c->tree, e->overflow_page, value, value_len);
        if (rc != BTREE_OK) { free(*key); return rc; }
    } else {
        *value_len = e->value_len;
        *value = malloc(*value_len);
        if (!*value) { free(*key); return BTREE_ERR; }
        memcpy(*value, e->value, *value_len);
    }

    c->position++;
    if (c->position >= c->count) c->done = 1;

    return BTREE_OK;
}

int cursor_close(CURSOR *c)
{
    if (!c) return BTREE_ERR;
    for (int i = 0; i < c->count; i++) {
        free(c->entries[i].key);
        free(c->entries[i].value);
    }
    free(c->entries);
    free(c);
    return BTREE_OK;
}