/*
 * test_btree.c — Comprehensive test harness for B-tree storage engine
 *
 * Tests: basic operations, splits/merges, large datasets, variable-length
 * data, cursor iteration, persistence, and edge cases.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include <unistd.h>

#include "../src/btree.h"

#define TEST_DB "test_btree.db"

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) printf("  TEST: %-50s ", name);
#define PASS() do { printf("PASS\n"); tests_passed++; } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); tests_failed++; } while(0)
#define ASSERT(cond, msg) do { \
    if (!(cond)) { FAIL(msg); return; } \
} while(0)

/* ============================================================
 * Helper: generate a key/value pair from an integer
 * ============================================================ */

static void make_key(int i, char *buf, size_t *len) {
    *len = snprintf(buf, 128, "key_%06d", i);
}

static void make_value(int i, char *buf, size_t *len) {
    *len = snprintf(buf, 256, "value_%06d_data_here", i);
}

/* ============================================================
 * Test: Empty tree operations
 * ============================================================ */

static void test_empty_tree(void) {
    TEST("empty tree - search returns not found");

    BTREE *tree = btree_open(TEST_DB, 3);
    ASSERT(tree != NULL, "failed to open tree");

    void *val;
    size_t vlen;
    int rc = btree_search(tree, "nonexistent", 11, &val, &vlen);
    ASSERT(rc == BTREE_NOTFOUND, "search on empty tree should return NOTFOUND");

    btree_close(tree);
    unlink(TEST_DB);
    PASS();
}

/* ============================================================
 * Test: Single key insert/search/delete
 * ============================================================ */

static void test_single_key(void) {
    TEST("single key insert, search, delete");

    BTREE *tree = btree_open(TEST_DB, 3);
    ASSERT(tree != NULL, "failed to open tree");

    char key[] = "hello";
    char val[] = "world";
    int rc;

    rc = btree_insert(tree, key, strlen(key), val, strlen(val));
    ASSERT(rc == BTREE_OK, "insert should succeed");

    void *result;
    size_t result_len;
    rc = btree_search(tree, key, strlen(key), &result, &result_len);
    ASSERT(rc == BTREE_OK, "search should find key");
    ASSERT(result_len == strlen(val), "value length mismatch");
    ASSERT(memcmp(result, val, result_len) == 0, "value content mismatch");
    free(result);

    rc = btree_delete(tree, key, strlen(key));
    ASSERT(rc == BTREE_OK, "delete should succeed");

    rc = btree_search(tree, key, strlen(key), &result, &result_len);
    ASSERT(rc == BTREE_NOTFOUND, "search after delete should return NOTFOUND");

    btree_close(tree);
    unlink(TEST_DB);
    PASS();
}

/* ============================================================
 * Test: Duplicate key insertion
 * ============================================================ */

static void test_duplicate_key(void) {
    TEST("duplicate key insertion returns BTREE_EXISTS");

    BTREE *tree = btree_open(TEST_DB, 3);
    ASSERT(tree != NULL, "failed to open tree");

    char key[] = "dup";
    char val1[] = "first";
    char val2[] = "second";

    int rc = btree_insert(tree, key, strlen(key), val1, strlen(val1));
    ASSERT(rc == BTREE_OK, "first insert should succeed");

    rc = btree_insert(tree, key, strlen(key), val2, strlen(val2));
    ASSERT(rc == BTREE_EXISTS, "duplicate insert should return EXISTS");

    /* Verify original value is intact */
    void *result;
    size_t result_len;
    rc = btree_search(tree, key, strlen(key), &result, &result_len);
    ASSERT(rc == BTREE_OK, "should still find original key");
    ASSERT(memcmp(result, val1, result_len) == 0, "original value should be preserved");
    free(result);

    btree_close(tree);
    unlink(TEST_DB);
    PASS();
}

/* ============================================================
 * Test: Sequential insertion (forces splits)
 * ============================================================ */

static void test_sequential_insert(int order, int count) {
    char name[128];
    snprintf(name, sizeof(name), "sequential insert order=%d count=%d", order, count);
    TEST(name);

    BTREE *tree = btree_open(TEST_DB, order);
    ASSERT(tree != NULL, "failed to open tree");

    char key_buf[128], val_buf[256];
    size_t klen, vlen;

    /* Insert keys in order */
    for (int i = 0; i < count; i++) {
        make_key(i, key_buf, &klen);
        make_value(i, val_buf, &vlen);
        int rc = btree_insert(tree, key_buf, klen, val_buf, vlen);
        ASSERT(rc == BTREE_OK || rc == BTREE_EXISTS, "insert should succeed");
    }

    /* Verify all keys */
    int found = 0;
    for (int i = 0; i < count; i++) {
        make_key(i, key_buf, &klen);
        make_value(i, val_buf, &vlen);

        void *result;
        size_t result_len;
        int rc = btree_search(tree, key_buf, klen, &result, &result_len);
        if (rc == BTREE_OK) {
            ASSERT(result_len == vlen, "value length mismatch");
            ASSERT(memcmp(result, val_buf, result_len) == 0, "value content mismatch");
            free(result);
            found++;
        }
    }
    ASSERT(found == count, "all keys should be found");

    btree_stats(tree);
    btree_close(tree);
    unlink(TEST_DB);
    PASS();
}

/* ============================================================
 * Test: Reverse insertion
 * ============================================================ */

static void test_reverse_insert(int order, int count) {
    char name[128];
    snprintf(name, sizeof(name), "reverse insert order=%d count=%d", order, count);
    TEST(name);

    BTREE *tree = btree_open(TEST_DB, order);
    ASSERT(tree != NULL, "failed to open tree");

    char key_buf[128], val_buf[256];
    size_t klen, vlen;

    /* Insert keys in reverse order */
    for (int i = count - 1; i >= 0; i--) {
        make_key(i, key_buf, &klen);
        make_value(i, val_buf, &vlen);
        int rc = btree_insert(tree, key_buf, klen, val_buf, vlen);
        ASSERT(rc == BTREE_OK, "insert should succeed");
    }

    /* Verify all keys */
    for (int i = 0; i < count; i++) {
        make_key(i, key_buf, &klen);
        make_value(i, val_buf, &vlen);

        void *result;
        size_t result_len;
        int rc = btree_search(tree, key_buf, klen, &result, &result_len);
        ASSERT(rc == BTREE_OK, "search should find key");
        ASSERT(memcmp(result, val_buf, result_len) == 0, "value content mismatch");
        free(result);
    }

    btree_close(tree);
    unlink(TEST_DB);
    PASS();
}

/* ============================================================
 * Test: Random insertion
 * ============================================================ */

static void test_random_insert(int order, int count) {
    char name[128];
    snprintf(name, sizeof(name), "random insert order=%d count=%d", order, count);
    TEST(name);

    BTREE *tree = btree_open(TEST_DB, order);
    ASSERT(tree != NULL, "failed to open tree");

    /* Generate shuffled indices */
    int *indices = malloc(count * sizeof(int));
    for (int i = 0; i < count; i++) indices[i] = i;
    srand(42); /* deterministic seed */
    for (int i = count - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        int tmp = indices[i]; indices[i] = indices[j]; indices[j] = tmp;
    }

    char key_buf[128], val_buf[256];
    size_t klen, vlen;

    for (int i = 0; i < count; i++) {
        make_key(indices[i], key_buf, &klen);
        make_value(indices[i], val_buf, &vlen);
        int rc = btree_insert(tree, key_buf, klen, val_buf, vlen);
        ASSERT(rc == BTREE_OK, "insert should succeed");
    }

    /* Verify all keys in order */
    for (int i = 0; i < count; i++) {
        make_key(i, key_buf, &klen);
        make_value(i, val_buf, &vlen);

        void *result;
        size_t result_len;
        int rc = btree_search(tree, key_buf, klen, &result, &result_len);
        ASSERT(rc == BTREE_OK, "search should find key");
        ASSERT(memcmp(result, val_buf, result_len) == 0, "value mismatch");
        free(result);
    }

    btree_close(tree);
    free(indices);
    unlink(TEST_DB);
    PASS();
}

/* ============================================================
 * Test: Large dataset (1000+ keys)
 * ============================================================ */

static void test_large_dataset(void) {
    TEST("large dataset (1000 keys, order=3)");

    BTREE *tree = btree_open(TEST_DB, 3);
    ASSERT(tree != NULL, "failed to open tree");

    const int N = 1000;
    char key_buf[128], val_buf[256];
    size_t klen, vlen;

    for (int i = 0; i < N; i++) {
        make_key(i, key_buf, &klen);
        make_value(i, val_buf, &vlen);
        int rc = btree_insert(tree, key_buf, klen, val_buf, vlen);
        ASSERT(rc == BTREE_OK, "insert should succeed");
    }

    /* Verify random subset */
    int verified = 0;
    for (int i = 0; i < N; i += 7) {
        make_key(i, key_buf, &klen);
        make_value(i, val_buf, &vlen);

        void *result;
        size_t result_len;
        int rc = btree_search(tree, key_buf, klen, &result, &result_len);
        ASSERT(rc == BTREE_OK, "search should find key");
        ASSERT(memcmp(result, val_buf, result_len) == 0, "value mismatch");
        free(result);
        verified++;
    }
    printf("(verified %d) ", verified);

    btree_stats(tree);
    btree_close(tree);
    unlink(TEST_DB);
    PASS();
}

/* ============================================================
 * Test: Variable-length keys and values
 * ============================================================ */

static void test_variable_length(void) {
    TEST("variable-length keys and values");

    BTREE *tree = btree_open(TEST_DB, 3);
    ASSERT(tree != NULL, "failed to open tree");

    /* Short keys */
    int rc = btree_insert(tree, "a", 1, "val_a", 5);
    ASSERT(rc == BTREE_OK, "short key insert");

    /* Medium keys */
    rc = btree_insert(tree, "medium_key_here", 15, "medium_val", 10);
    ASSERT(rc == BTREE_OK, "medium key insert");

    /* Long keys (up to MAX_INLINE_KEY) */
    char long_key[100];
    char long_val[200];
    memset(long_key, 'K', 99); long_key[99] = '\0';
    memset(long_val, 'V', 199); long_val[199] = '\0';

    rc = btree_insert(tree, long_key, 99, long_val, 199);
    ASSERT(rc == BTREE_OK, "long key insert");

    /* Verify long key search */
    void *result;
    size_t result_len;
    rc = btree_search(tree, long_key, 99, &result, &result_len);
    ASSERT(rc == BTREE_OK, "long key search");
    ASSERT(result_len == 199, "long value length mismatch");
    ASSERT(memcmp(result, long_val, 199) == 0, "long value content mismatch");
    free(result);

    /* Verify short key still works */
    rc = btree_search(tree, "a", 1, &result, &result_len);
    ASSERT(rc == BTREE_OK, "short key search");
    ASSERT(result_len == 5, "short value length");
    free(result);

    btree_close(tree);
    unlink(TEST_DB);
    PASS();
}

/* ============================================================
 * Test: Binary key/value (non-string data)
 * ============================================================ */

static void test_binary_data(void) {
    TEST("binary (non-string) keys and values");

    BTREE *tree = btree_open(TEST_DB, 3);
    ASSERT(tree != NULL, "failed to open tree");

    /* Keys and values with null bytes */
    uint8_t key1[] = {0x01, 0x00, 0x02, 0x00, 0x03};
    uint8_t val1[] = {0xFF, 0x00, 0xFE, 0x00};
    uint8_t key2[] = {0x01, 0x00};  /* prefix of key1 */
    uint8_t val2[] = {0xAA};

    int rc = btree_insert(tree, key1, sizeof(key1), val1, sizeof(val1));
    ASSERT(rc == BTREE_OK, "binary key1 insert");

    rc = btree_insert(tree, key2, sizeof(key2), val2, sizeof(val2));
    ASSERT(rc == BTREE_OK, "binary key2 insert (prefix of key1)");

    /* Verify both */
    void *result;
    size_t result_len;

    rc = btree_search(tree, key1, sizeof(key1), &result, &result_len);
    ASSERT(rc == BTREE_OK, "binary key1 search");
    ASSERT(result_len == sizeof(val1), "binary val1 length");
    ASSERT(memcmp(result, val1, result_len) == 0, "binary val1 content");
    free(result);

    rc = btree_search(tree, key2, sizeof(key2), &result, &result_len);
    ASSERT(rc == BTREE_OK, "binary key2 search");
    ASSERT(result_len == sizeof(val2), "binary val2 length");
    ASSERT(memcmp(result, val2, result_len) == 0, "binary val2 content");
    free(result);

    btree_close(tree);
    unlink(TEST_DB);
    PASS();
}

/* ============================================================
 * Test: Overflow values (> MAX_INLINE_VAL)
 * ============================================================ */

static void test_overflow_values(void) {
    TEST("overflow values (large values)");

    BTREE *tree = btree_open(TEST_DB, 3);
    ASSERT(tree != NULL, "failed to open tree");

    /* Value larger than MAX_INLINE_VAL (256 bytes) */
    size_t big_len = 1000;
    char *big_val = malloc(big_len);
    for (size_t i = 0; i < big_len; i++) big_val[i] = (char)('A' + (i % 26));

    int rc = btree_insert(tree, "bigkey", 6, big_val, big_len);
    ASSERT(rc == BTREE_OK, "overflow insert");

    /* Also insert a normal key alongside */
    rc = btree_insert(tree, "smallkey", 8, "small", 5);
    ASSERT(rc == BTREE_OK, "small key insert");

    /* Verify overflow value */
    void *result;
    size_t result_len;
    rc = btree_search(tree, "bigkey", 6, &result, &result_len);
    ASSERT(rc == BTREE_OK, "overflow search");
    ASSERT(result_len == big_len, "overflow value length");
    ASSERT(memcmp(result, big_val, big_len) == 0, "overflow value content");
    free(result);

    /* Verify small value still works */
    rc = btree_search(tree, "smallkey", 8, &result, &result_len);
    ASSERT(rc == BTREE_OK, "small search after overflow");
    ASSERT(result_len == 5, "small value length");
    free(result);

    /* Delete overflow value */
    rc = btree_delete(tree, "bigkey", 6);
    ASSERT(rc == BTREE_OK, "overflow delete");

    rc = btree_search(tree, "bigkey", 6, &result, &result_len);
    ASSERT(rc == BTREE_NOTFOUND, "overflow key should be gone after delete");

    btree_close(tree);
    free(big_val);
    unlink(TEST_DB);
    PASS();
}

/* ============================================================
 * Test: Cursor iteration (forward scan)
 * ============================================================ */

static void test_cursor_iteration(int count) {
    char name[128];
    snprintf(name, sizeof(name), "cursor iteration (%d keys)", count);
    TEST(name);

    BTREE *tree = btree_open(TEST_DB, 3);
    ASSERT(tree != NULL, "failed to open tree");

    char key_buf[128], val_buf[256];
    size_t klen, vlen;

    for (int i = 0; i < count; i++) {
        make_key(i, key_buf, &klen);
        make_value(i, val_buf, &vlen);
        btree_insert(tree, key_buf, klen, val_buf, vlen);
    }

    /* Iterate and collect keys in order */
    CURSOR *c = btree_cursor(tree);
    ASSERT(c != NULL, "cursor creation");

    int iterated = 0;
    int last_key_num = -1;
    void *key, *value;
    size_t key_len, value_len;

    while (cursor_next(c, &key, &key_len, &value, &value_len) == BTREE_OK) {
        /* Parse key number — null-terminate for sscanf */
        char keystr[128];
        memcpy(keystr, key, key_len < 127 ? key_len : 127);
        keystr[key_len < 127 ? key_len : 127] = '\0';
        int key_num = -1;
        sscanf(keystr, "key_%d", &key_num);

        /* Keys should be in sorted order */
        ASSERT(key_num > last_key_num, "keys should be in ascending order");
        last_key_num = key_num;

        iterated++;
        free(key);
        free(value);
    }

    ASSERT(iterated == count, "should iterate all keys");

    cursor_close(c);
    btree_close(tree);
    unlink(TEST_DB);
    PASS();
}

/* ============================================================
 * Test: Persistence (close and reopen)
 * ============================================================ */

static void test_persistence(void) {
    TEST("persistence (close and reopen)");

    const int N = 50;
    BTREE *tree = btree_open(TEST_DB, 3);
    ASSERT(tree != NULL, "failed to open tree");

    char key_buf[128], val_buf[256];
    size_t klen, vlen;

    for (int i = 0; i < N; i++) {
        make_key(i, key_buf, &klen);
        make_value(i, val_buf, &vlen);
        btree_insert(tree, key_buf, klen, val_buf, vlen);
    }

    btree_close(tree);

    /* Reopen */
    tree = btree_open(TEST_DB, 3);
    ASSERT(tree != NULL, "failed to reopen tree");

    /* Verify all keys survived */
    for (int i = 0; i < N; i++) {
        make_key(i, key_buf, &klen);
        make_value(i, val_buf, &vlen);

        void *result;
        size_t result_len;
        int rc = btree_search(tree, key_buf, klen, &result, &result_len);
        ASSERT(rc == BTREE_OK, "key should survive persistence");
        ASSERT(memcmp(result, val_buf, result_len) == 0, "value should survive persistence");
        free(result);
    }

    btree_close(tree);
    unlink(TEST_DB);
    PASS();
}

/* ============================================================
 * Test: Delete and verify
 * ============================================================ */

static void test_delete_and_verify(void) {
    TEST("delete keys and verify remaining");

    const int N = 30;
    BTREE *tree = btree_open(TEST_DB, 3);
    ASSERT(tree != NULL, "failed to open tree");

    char key_buf[128], val_buf[256];
    size_t klen, vlen;

    for (int i = 0; i < N; i++) {
        make_key(i, key_buf, &klen);
        make_value(i, val_buf, &vlen);
        btree_insert(tree, key_buf, klen, val_buf, vlen);
    }

    /* Delete every other key */
    for (int i = 0; i < N; i += 2) {
        make_key(i, key_buf, &klen);
        int rc = btree_delete(tree, key_buf, klen);
        ASSERT(rc == BTREE_OK, "delete should succeed");
    }

    /* Verify deleted keys are gone */
    for (int i = 0; i < N; i += 2) {
        make_key(i, key_buf, &klen);
        void *result;
        size_t result_len;
        int rc = btree_search(tree, key_buf, klen, &result, &result_len);
        ASSERT(rc == BTREE_NOTFOUND, "deleted key should be gone");
    }

    /* Verify remaining keys are intact */
    for (int i = 1; i < N; i += 2) {
        make_key(i, key_buf, &klen);
        make_value(i, val_buf, &vlen);

        void *result;
        size_t result_len;
        int rc = btree_search(tree, key_buf, klen, &result, &result_len);
        ASSERT(rc == BTREE_OK, "remaining key should be found");
        ASSERT(memcmp(result, val_buf, result_len) == 0, "remaining value intact");
        free(result);
    }

    btree_close(tree);
    unlink(TEST_DB);
    PASS();
}

/* ============================================================
 * Test: Delete all keys
 * ============================================================ */

static void test_delete_all(void) {
    TEST("delete all keys (tree becomes empty)");

    const int N = 20;
    BTREE *tree = btree_open(TEST_DB, 3);
    ASSERT(tree != NULL, "failed to open tree");

    char key_buf[128], val_buf[256];
    size_t klen, vlen;

    for (int i = 0; i < N; i++) {
        make_key(i, key_buf, &klen);
        make_value(i, val_buf, &vlen);
        btree_insert(tree, key_buf, klen, val_buf, vlen);
    }

    /* Delete all keys */
    for (int i = 0; i < N; i++) {
        make_key(i, key_buf, &klen);
        int rc = btree_delete(tree, key_buf, klen);
        ASSERT(rc == BTREE_OK, "delete should succeed");
    }

    /* Verify all are gone */
    for (int i = 0; i < N; i++) {
        make_key(i, key_buf, &klen);
        void *result;
        size_t result_len;
        int rc = btree_search(tree, key_buf, klen, &result, &result_len);
        ASSERT(rc == BTREE_NOTFOUND, "all keys should be deleted");
    }

    btree_stats(tree);
    btree_close(tree);
    unlink(TEST_DB);
    PASS();
}

/* ============================================================
 * Test: Delete non-existent key
 * ============================================================ */

static void test_delete_nonexistent(void) {
    TEST("delete non-existent key returns NOTFOUND");

    BTREE *tree = btree_open(TEST_DB, 3);
    ASSERT(tree != NULL, "failed to open tree");

    btree_insert(tree, "exists", 6, "yes", 3);

    int rc = btree_delete(tree, "nothere", 7);
    ASSERT(rc == BTREE_NOTFOUND, "delete of missing key should return NOTFOUND");

    /* Original key should still be there */
    void *result;
    size_t result_len;
    rc = btree_search(tree, "exists", 6, &result, &result_len);
    ASSERT(rc == BTREE_OK, "existing key should still be there");
    free(result);

    btree_close(tree);
    unlink(TEST_DB);
    PASS();
}

/* ============================================================
 * Test: Higher order B-tree
 * ============================================================ */

static void test_higher_order(void) {
    TEST("higher order B-tree (t=10)");

    BTREE *tree = btree_open(TEST_DB, 10);
    ASSERT(tree != NULL, "failed to open tree");

    const int N = 200;
    char key_buf[128], val_buf[256];
    size_t klen, vlen;

    for (int i = 0; i < N; i++) {
        make_key(i, key_buf, &klen);
        make_value(i, val_buf, &vlen);
        btree_insert(tree, key_buf, klen, val_buf, vlen);
    }

    /* Verify all */
    for (int i = 0; i < N; i++) {
        make_key(i, key_buf, &klen);
        make_value(i, val_buf, &vlen);

        void *result;
        size_t result_len;
        int rc = btree_search(tree, key_buf, klen, &result, &result_len);
        ASSERT(rc == BTREE_OK, "key should be found");
        ASSERT(memcmp(result, val_buf, result_len) == 0, "value should match");
        free(result);
    }

    btree_stats(tree);
    btree_close(tree);
    unlink(TEST_DB);
    PASS();
}

/* ============================================================
 * Test: Mixed insert/delete cycle
 * ============================================================ */

static void test_mixed_operations(void) {
    TEST("mixed insert and delete cycles");

    BTREE *tree = btree_open(TEST_DB, 3);
    ASSERT(tree != NULL, "failed to open tree");

    /* Insert batch 1 */
    for (int i = 0; i < 20; i++) {
        char key[32], val[32];
        size_t klen = snprintf(key, sizeof(key), "batch1_%d", i);
        size_t vlen = snprintf(val, sizeof(val), "val_%d", i);
        btree_insert(tree, key, klen, val, vlen);
    }

    /* Delete half */
    for (int i = 0; i < 20; i += 2) {
        char key[32];
        size_t klen = snprintf(key, sizeof(key), "batch1_%d", i);
        btree_delete(tree, key, klen);
    }

    /* Insert batch 2 */
    for (int i = 0; i < 20; i++) {
        char key[32], val[32];
        size_t klen = snprintf(key, sizeof(key), "batch2_%d", i);
        size_t vlen = snprintf(val, sizeof(val), "val2_%d", i);
        btree_insert(tree, key, klen, val, vlen);
    }

    /* Verify remaining from batch 1 */
    int found = 0;
    for (int i = 1; i < 20; i += 2) {
        char key[32];
        size_t klen = snprintf(key, sizeof(key), "batch1_%d", i);
        void *result;
        size_t result_len;
        if (btree_search(tree, key, klen, &result, &result_len) == BTREE_OK) {
            found++;
            free(result);
        }
    }
    ASSERT(found == 10, "should find 10 remaining batch1 keys");

    /* Verify batch 2 */
    found = 0;
    for (int i = 0; i < 20; i++) {
        char key[32];
        size_t klen = snprintf(key, sizeof(key), "batch2_%d", i);
        void *result;
        size_t result_len;
        if (btree_search(tree, key, klen, &result, &result_len) == BTREE_OK) {
            found++;
            free(result);
        }
    }
    ASSERT(found == 20, "should find all batch2 keys");

    btree_close(tree);
    unlink(TEST_DB);
    PASS();
}

/* ============================================================
 * Test: Very large value (multi-page overflow chain)
 * ============================================================ */

static void test_large_overflow(void) {
    TEST("large value (multi-page overflow chain)");

    BTREE *tree = btree_open(TEST_DB, 3);
    ASSERT(tree != NULL, "failed to open tree");

    /* Value that requires multiple overflow pages (>4090 bytes) */
    size_t big_len = 10000;
    char *big_val = malloc(big_len);
    for (size_t i = 0; i < big_len; i++) big_val[i] = (char)('A' + (i % 26));

    int rc = btree_insert(tree, "hugekey", 7, big_val, big_len);
    ASSERT(rc == BTREE_OK, "huge insert");

    /* Also a small key to make sure nothing got corrupted */
    rc = btree_insert(tree, "tiny", 4, "x", 1);
    ASSERT(rc == BTREE_OK, "tiny insert after huge");

    /* Verify huge value */
    void *result;
    size_t result_len;
    rc = btree_search(tree, "hugekey", 7, &result, &result_len);
    ASSERT(rc == BTREE_OK, "huge search");
    ASSERT(result_len == big_len, "huge value length");
    ASSERT(memcmp(result, big_val, big_len) == 0, "huge value content");
    free(result);

    /* Verify tiny still works */
    rc = btree_search(tree, "tiny", 4, &result, &result_len);
    ASSERT(rc == BTREE_OK, "tiny search after huge");
    ASSERT(result_len == 1 && ((char*)result)[0] == 'x', "tiny value intact");
    free(result);

    /* Delete the huge value */
    rc = btree_delete(tree, "hugekey", 7);
    ASSERT(rc == BTREE_OK, "huge delete");

    rc = btree_search(tree, "hugekey", 7, &result, &result_len);
    ASSERT(rc == BTREE_NOTFOUND, "huge should be gone");

    btree_close(tree);
    free(big_val);
    unlink(TEST_DB);
    PASS();
}

/* ============================================================
 * Main
 * ============================================================ */

int main(void) {
    printf("=== B-tree Storage Engine Tests ===\n\n");

    /* Basic operations */
    printf("--- Basic Operations ---\n");
    test_empty_tree();
    test_single_key();
    test_duplicate_key();

    /* Splits and merges */
    printf("\n--- Splits and Merges ---\n");
    test_sequential_insert(3, 50);
    test_sequential_insert(5, 100);
    test_reverse_insert(3, 50);
    test_reverse_insert(5, 100);
    test_random_insert(3, 100);
    test_random_insert(5, 200);

    /* Large datasets */
    printf("\n--- Large Datasets ---\n");
    test_large_dataset();
    test_higher_order();

    /* Variable-length data */
    printf("\n--- Variable-Length Data ---\n");
    test_variable_length();
    test_binary_data();
    test_overflow_values();
    test_large_overflow();

    /* Cursor iteration */
    printf("\n--- Cursor Iteration ---\n");
    test_cursor_iteration(10);
    test_cursor_iteration(50);
    test_cursor_iteration(200);

    /* Persistence */
    printf("\n--- Persistence ---\n");
    test_persistence();

    /* Deletion */
    printf("\n--- Deletion ---\n");
    test_delete_and_verify();
    test_delete_all();
    test_delete_nonexistent();
    test_mixed_operations();

    /* Summary */
    printf("\n=== Results: %d passed, %d failed ===\n",
           tests_passed, tests_failed);

    return tests_failed > 0 ? 1 : 0;
}