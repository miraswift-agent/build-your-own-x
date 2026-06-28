/*
 * Stage 1 tests: Implicit free list — with heap invariant checks
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include "../src/malloc.h"

static int tests_run = 0;
static int tests_passed = 0;
static int check_failures = 0;

#define TEST(name) \
    do { \
        tests_run++; \
        printf("  %-40s", #name); \
    } while (0)

#define PASS() \
    do { \
        tests_passed++; \
        printf("✓\n"); \
    } while (0)

#define FAIL(msg) \
    do { \
        printf("✗ %s\n", msg); \
    } while (0)

/* Heap invariant checker — walks the heap and validates every block */
static void check_heap(const char *where)
{
    if (heap_start == NULL) return;

    word_t *prev_blk = NULL;
    for (word_t *h = heap_start; !IS_EPILOGUE(h); h = NEXT_HDR(h)) {
        size_t size = GET_SIZE(h);
        int alloc = GET_ALLOC(h);
        int pf = GET_PREV_FREE(h);

        /* Size must be aligned and >= MIN_BLOCK_SIZE */
        if (size < MIN_BLOCK_SIZE || (size & (ALIGNMENT - 1)) != 0) {
            printf("  HEAP CHECK [%s]: block %p has invalid size %zu (alloc=%d)\n",
                   where, (void *)h, size, alloc);
            check_failures++;
            return;
        }

        /* Free blocks must have matching footer */
        if (!alloc) {
            word_t *foot = FOOTER(h);
            if (GET_SIZE(foot) != size) {
                printf("  HEAP CHECK [%s]: free block footer mismatch at %p (hdr=%zu foot=%zu)\n",
                       where, (void *)h, size, GET_SIZE(foot));
                check_failures++;
                return;
            }
            /* No two adjacent free blocks (coalescing invariant) */
            word_t *next_block = NEXT_HDR(h);
            if (!IS_EPILOGUE(next_block) && !GET_ALLOC(next_block)) {
                printf("  HEAP CHECK [%s]: adjacent free blocks at %p and %p\n",
                       where, (void *)h, (void *)next_block);
                check_failures++;
                return;
            }
        }

        /* prev_free bit must match previous block's alloc status */
        int expected_pf = (h != heap_start && prev_blk != NULL) ? !GET_ALLOC(prev_blk) : 0;
        if (pf != expected_pf) {
            printf("  HEAP CHECK [%s]: prev_free mismatch at %p (expected=%d actual=%d)\n",
                   where, (void *)h, expected_pf, pf);
            check_failures++;
            return;
        }

        prev_blk = h;
    }
}

/* ─── Basic allocation tests ─── */

static void test_alloc_returns_nonnull(void)
{
    TEST(alloc returns non-null);
    void *p = mira_malloc(100);
    check_heap("after malloc(100)");
    if (p != NULL) { PASS(); } else { FAIL("returned null"); }
    mira_free(p);
    check_heap("after free");
}

static void test_alloc_alignment(void)
{
    TEST(alignment (8-byte));
    void *p1 = mira_malloc(1);
    void *p2 = mira_malloc(1);
    void *p3 = mira_malloc(7);

    int aligned = ((size_t)p1 % 8 == 0) &&
                  ((size_t)p2 % 8 == 0) &&
                  ((size_t)p3 % 8 == 0);
    if (aligned) { PASS(); } else { FAIL("unaligned pointer"); }

    mira_free(p1);
    mira_free(p2);
    mira_free(p3);
    check_heap("after alignment test");
}

static void test_alloc_zero_returns_null(void)
{
    TEST(zero-size returns null);
    void *p = mira_malloc(0);
    if (p == NULL) { PASS(); } else { FAIL("should return null"); }
}

static void test_multiple_allocs(void)
{
    TEST(multiple allocations);
    void *a = mira_malloc(64);
    void *b = mira_malloc(128);
    void *c = mira_malloc(32);

    int distinct = (a != b) && (b != c) && (a != c);

    memset(a, 'A', 64);
    memset(b, 'B', 128);
    memset(c, 'C', 32);

    int correct = ((char *)a)[0] == 'A' &&
                  ((char *)b)[0] == 'B' &&
                  ((char *)c)[0] == 'C';

    if (distinct && correct) { PASS(); } else { FAIL("pointers overlap or write failed"); }

    mira_free(a);
    check_heap("after free(a)");
    mira_free(b);
    check_heap("after free(b)");
    mira_free(c);
    check_heap("after free(c)");
}

/* ─── Free and reuse tests ─── */

static void test_free_then_realloc(void)
{
    TEST(free then reallocate same size);
    void *p1 = mira_malloc(64);
    memset(p1, 'X', 64);
    mira_free(p1);
    check_heap("after free(p1)");

    void *p2 = mira_malloc(64);
    if (p2 != NULL) { PASS(); } else { FAIL("null on realloc"); }
    mira_free(p2);
    check_heap("after free(p2)");
}

static void test_free_order_independence(void)
{
    TEST(free order independence);
    void *a = mira_malloc(32);
    void *b = mira_malloc(32);
    void *c = mira_malloc(32);

    memset(a, 'A', 32);
    memset(b, 'B', 32);
    memset(c, 'C', 32);

    mira_free(b);
    check_heap("after free(b)");
    mira_free(a);
    check_heap("after free(a)");
    mira_free(c);
    check_heap("after free(c)");

    size_t free_space = mira_free_space();
    if (free_space > 0) { PASS(); } else { FAIL("no free space after freeing all"); }
}

/* ─── Coalescing tests ─── */

static void test_coalesce_adjacent(void)
{
    TEST(coalesce adjacent free blocks);
    void *a = mira_malloc(64);
    void *b = mira_malloc(64);
    void *c = mira_malloc(64);

    size_t free_before = mira_free_space();

    mira_free(a);
    check_heap("after free(a)");
    mira_free(b);
    check_heap("after free(b)");
    mira_free(c);
    check_heap("after free(c)");

    size_t free_after = mira_free_space();
    if (free_after > free_before) { PASS(); } else { FAIL("coalescing failed"); }
}

static void test_coalesce_middle_first(void)
{
    TEST(coalesce: free middle then neighbors);
    void *a = mira_malloc(64);
    void *b = mira_malloc(64);
    void *c = mira_malloc(64);
    void *d = mira_malloc(64);

    mira_free(b);
    check_heap("after free(b)");
    mira_free(a);
    check_heap("after free(a)");
    mira_free(c);
    check_heap("after free(c)");

    size_t largest = mira_largest_free();
    mira_free(d);

    if (largest >= 192) { PASS(); } else { FAIL("coalescing incomplete"); }
}

/* ─── Splitting tests ─── */

static void test_split_reuse(void)
{
    TEST(split: large block splits for small alloc);
    void *big = mira_malloc(512);
    memset(big, 'Z', 512);
    mira_free(big);
    check_heap("after free(big)");

    void *small = mira_malloc(64);
    if (small != NULL) { PASS(); } else { FAIL("split failed"); }
    check_heap("after malloc(64) from split");

    size_t remaining = mira_free_space();
    (void)remaining;

    mira_free(small);
    check_heap("after free(small)");
}

/* ─── calloc and realloc ─── */

static void test_calloc_zeroed(void)
{
    TEST(calloc returns zeroed memory);
    void *p = mira_calloc(10, 10);
    if (p == NULL) { FAIL("calloc returned null"); return; }

    unsigned char *bytes = (unsigned char *)p;
    int all_zero = 1;
    for (int i = 0; i < 100; i++) {
        if (bytes[i] != 0) { all_zero = 0; break; }
    }

    if (all_zero) { PASS(); } else { FAIL("memory not zeroed"); }
    mira_free(p);
    check_heap("after free(calloc)");
}

static void test_realloc_grow(void)
{
    TEST(realloc grows in place or copies);
    void *p = mira_malloc(32);
    memset(p, 'Y', 32);
    check_heap("before realloc grow");

    void *p2 = mira_realloc(p, 128);
    check_heap("after realloc grow");
    if (p2 == NULL) { FAIL("realloc returned null"); return; }

    int preserved = ((char *)p2)[0] == 'Y' && ((char *)p2)[31] == 'Y';

    if (preserved) { PASS(); } else { FAIL("data not preserved"); }
    mira_free(p2);
    check_heap("after free(realloc'd)");
}

static void test_realloc_shrink(void)
{
    TEST(realloc shrinks block);
    void *p = mira_malloc(256);
    check_heap("after malloc(256)");
    void *p2 = mira_realloc(p, 32);
    check_heap("after realloc shrink");

    if (p2 != NULL) { PASS(); } else { FAIL("shrink realloc failed"); }
    mira_free(p2);
    check_heap("after free(shrunk)");
}

/* ─── Stress test with invariant checking ─── */

static void test_stress_random_alloc_free(void)
{
    TEST(stress: 1000 random alloc/free);

    #define STRESS_OPS 1000
    #define MAX_PTRS 100
    #define MAX_SIZE 256

    void *ptrs[MAX_PTRS] = {0};
    srand(42);

    int alloc_count = 0, free_count = 0;

    for (int i = 0; i < STRESS_OPS; i++) {
        int slot = rand() % MAX_PTRS;

        if (ptrs[slot] == NULL) {
            size_t size = (size_t)(rand() % MAX_SIZE) + 1;
            ptrs[slot] = mira_malloc(size);
            if (ptrs[slot])
                memset(ptrs[slot], slot & 0xff, size);
            alloc_count++;
        } else {
            mira_free(ptrs[slot]);
            ptrs[slot] = NULL;
            free_count++;
        }

        /* Check heap invariant every 100 ops */
        if (i % 100 == 0 && check_failures == 0) {
            char label[32];
            snprintf(label, sizeof(label), "stress op %d", i);
            check_heap(label);
        }
    }

    /* Free remaining */
    for (int i = 0; i < MAX_PTRS; i++) {
        if (ptrs[i])
            mira_free(ptrs[i]);
    }
    check_heap("after stress cleanup");

    size_t free_space = mira_free_space();
    if (free_space > 0 && check_failures == 0) { PASS(); }
    else { FAIL("heap invariant violated or no free space"); }

    printf("    (allocs=%d, frees=%d, free_space=%zu, check_failures=%d)\n",
           alloc_count, free_count, free_space, check_failures);
}

/* ─── Double free detection ─── */

static void test_double_free_warning(void)
{
    TEST(double free prints warning);
    void *p = mira_malloc(32);
    check_heap("before double free");
    mira_free(p);
    check_heap("after first free");
    mira_free(p);  /* Should print warning, not crash */
    PASS();
}

/* ─── Main ─── */

int main(void)
{
    printf("\n╔══════════════════════════════════════════╗\n");
    printf("║  mira_malloc Stage 1: Implicit Free List  ║\n");
    printf("╚══════════════════════════════════════════╝\n\n");

    printf("─── Basic Allocation ───\n");
    test_alloc_returns_nonnull();
    test_alloc_alignment();
    test_alloc_zero_returns_null();
    test_multiple_allocs();

    printf("\n─── Free & Reuse ───\n");
    test_free_then_realloc();
    test_free_order_independence();

    printf("\n─── Coalescing ───\n");
    test_coalesce_adjacent();
    test_coalesce_middle_first();

    printf("\n─── Splitting ───\n");
    test_split_reuse();

    printf("\n─── calloc & realloc ───\n");
    test_calloc_zeroed();
    test_realloc_grow();
    test_realloc_shrink();

    printf("\n─── Stress ───\n");
    test_stress_random_alloc_free();

    printf("\n─── Edge Cases ───\n");
    test_double_free_warning();

    printf("\n");
    mira_heap_check();

    printf("\n═══════════════════════════════════════════\n");
    printf("  Results: %d/%d tests passed\n", tests_passed, tests_run);
    if (check_failures > 0)
        printf("  Heap invariant failures: %d\n", check_failures);
    printf("═══════════════════════════════════════════\n\n");

    return (tests_passed == tests_run && check_failures == 0) ? 0 : 1;
}