/*
 * btree.h — Public API for B-tree storage engine
 *
 * Keys and values are arbitrary byte sequences.
 * Supports insert, search, delete, and cursor-based iteration.
 * Data is persisted to a page-based file.
 */

#ifndef BTREE_H
#define BTREE_H

#include <stddef.h>
#include <stdint.h>

/* Result codes */
#define BTREE_OK        0
#define BTREE_ERR      -1
#define BTREE_NOTFOUND -2
#define BTREE_EXISTS   -3

/* Forward declarations */
typedef struct BTREE  BTREE;
typedef struct CURSOR CURSOR;

/* ============================================================
 * Core operations
 * ============================================================ */

BTREE* btree_open(const char *filename, int order);
int    btree_close(BTREE *tree);

int    btree_insert(BTREE *tree, const void *key, size_t key_len,
                    const void *value, size_t value_len);
int    btree_delete(BTREE *tree, const void *key, size_t key_len);
int    btree_search(BTREE *tree, const void *key, size_t key_len,
                    void **value, size_t *value_len);

/* ============================================================
 * Cursor operations
 * ============================================================ */

CURSOR* btree_cursor(BTREE *tree);
int     cursor_next(CURSOR *c, void **key, size_t *key_len,
                    void **value, size_t *value_len);
int     cursor_close(CURSOR *c);

/* ============================================================
 * Introspection
 * ============================================================ */

void btree_stats(BTREE *tree);

#endif /* BTREE_H */