/*
 * btree_internal.h — Internal structures shared between btree.c and cursor.c
 *
 * NOT part of the public API. These are implementation details.
 */

#ifndef BTREE_INTERNAL_H
#define BTREE_INTERNAL_H

#include "pager.h"
#include <stdint.h>

/* Node layout constants */
#define NODE_FLAG_LEAF   0x01
#define MAX_INLINE_KEY   128
#define MAX_INLINE_VAL   256

/* A cell in a node: inline key + value, or inline key + overflow pointer */
typedef struct {
    uint16_t key_len;
    uint16_t val_len;
    uint8_t  key[MAX_INLINE_KEY];
    union {
        uint8_t  inline_val[MAX_INLINE_VAL];
        uint32_t overflow_page;      /* page number if val_len > MAX_INLINE_VAL */
    } val_data;
    uint8_t  is_overflow;           /* 1 if value is in overflow pages */
} Cell;

/* In-memory node representation */
typedef struct {
    uint8_t  flag;          /* NODE_FLAG_LEAF or 0 */
    uint16_t nkeys;         /* number of keys in this node */
    uint32_t page_num;      /* page number of this node */
    Cell     cells[128];    /* cells (max 2t-1, t<=64) */
    uint32_t children[129]; /* child page numbers (max 2t) */
} Node;

/* BTREE handle — full definition for cursor access */
struct BTREE {
    Pager     pager;
    uint32_t  root_page;
    int       order;        /* min degree t */
    int       max_keys;     /* 2t - 1 */
    int       min_keys;     /* t - 1 */
    int       max_cells;    /* max cells per node that fit in a page */
    int       height;       /* approximate tree height */
    uint32_t  n_inserts;    /* stats */
    uint32_t  n_deletes;
    uint32_t  n_searches;
};

/* Internal function declarations */
int node_read(BTREE *tree, uint32_t page_num, Node *node);
int node_write(BTREE *tree, Node *node);
int overflow_read(BTREE *tree, uint32_t page_num, void **out, size_t *out_len);

#endif /* BTREE_INTERNAL_H */