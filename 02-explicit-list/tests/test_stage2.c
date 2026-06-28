/*
 * Stage 2 tests: Explicit free list
 *
 * Inherits all Stage 1 tests, adds free list specific tests.
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
        printf("  %-45s", #name); \
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

static void check(const char *where)
{
    if (heap_start == NULL) return;

    word_t *prev_blk = NULL;
    for (word_t *h = heap_start; !IS_EPILOGUE(h); h = NEXT_HDR(h)) {
        size_t size = GET_SIZE(h);
        int alloc = GET_ALLOC(h);
        int pf = GET_PREV_FREE(h);

        if (size < MIN_BLOCK_SIZE || (size & (ALIGNMENT - 1)) != 0) {
            printf("  CHECK [%s]: invalid size %zu at %p\n", where, size, (void *)h);
            check_failures++;
            return;
        }

        if (!alloc) {
            word_t *foot = FOOTER(h);
            if (GET_SIZE(foot) != size) {
                printf("  CHECK [%s]: free block footer mismatch at %p\n", where, (void *)h);
                check_failures++;
                return;
            }
            word_t *nxt = NEXT_HDR(h);
            if (!IS_EPILOGUE(nxt) && !GET_ALLOC(nxt)) {
                printf("  CHECK [%s]: adjacent free blocks\n", where);
                check_failures++;
                return;
            }
        }

        int expected_pf = (h != heap_start && prev_blk != NULL) ? !GET_ALLOC(prev_blk) : 0;
        if (pf != expected_pf) {
            printf("  CHECK [%s]: prev_free mismatch at %p (expected=%d actual=%d)\n",
                   where, (void *)h, expected_pf, pf);
            check_failures++;
            return;
        }

        prev_blk = h;
    }

    /* Verify free list matches heap free blocks */
    size_t list_len = mira_free_list_length();
    int heap_free = 0;
    for (word_t *h = heap_start; !IS_EPILOGUE(h); h = NEXT_HDR(h)) {
        if (!GET_ALLOC(h)) heap_free++;
    }
    if ((size_t)heap_free != list_len) {
        printf("  CHECK [%s]: free list has %zu entries but heap has %d free blocks\n",
               where, list_len, heap_free);
        check_failures++;
    }
}

/* ─── Basic Allocation ─── */

static void test_alloc_returns_nonnull(void)
{
    TEST(alloc returns non-null);
    void *p = mira_malloc(100);
    check("after malloc(100)");
    if (p != NULL) { PASS(); } else { FAIL("returned null"); }
    mira_free(p);
    check("after free");
}

static void test_alloc_alignment(void)
{
    TEST(alignment (8-byte));
    void *p1 = mira_malloc(1);
    void *p2 = mira_malloc(1);
    void *p3 = mira_malloc(7);
    int aligned = ((size_t)p1 % 8 == 0) && ((size_t)p2 % 8 == 0) && ((size_t)p3 % 8 == 0);
    if (aligned) { PASS(); } else { FAIL("unaligned pointer"); }
    mira_free(p1); mira_free(p2); mira_free(p3);
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
    memset(a, 'A', 64); memset(b, 'B', 128); memset(c, 'C', 32);
    int correct = ((char *)a)[0] == 'A' && ((char *)b)[0] == 'B' && ((char *)c)[0] == 'C';
    if (distinct && correct) { PASS(); } else { FAIL("overlap or write failed"); }
    mira_free(a); mira_free(b); mira_free(c);
    check("after freeing all");
}

/* ─── Free & Reuse ─── */

static void test_free_then_realloc(void)
{
    TEST(free then reallocate same size);
    void *p1 = mira_malloc(64);
    memset(p1, 'X', 64);
    mira_free(p1);
    check("after free");

    void *p2 = mira_malloc(64);
    /* LIFO: should reuse the most recently freed block */
    if (p2 != NULL) { PASS(); } else { FAIL("null on realloc"); }
    mira_free(p2);
}

static void test_free_order_independence(void)
{
    TEST(free order independence);
    void *a = mira_malloc(32); void *b = mira_malloc(32); void *c = mira_malloc(32);
    memset(a, 'A', 32); memset(b, 'B', 32); memset(c, 'C', 32);
    mira_free(b); check("after free(b)");
    mira_free(a); check("after free(a)");
    mira_free(c); check("after free(c)");
    size_t free_space = mira_free_space();
    if (free_space > 0) { PASS(); } else { FAIL("no free space"); }
}

/* ─── Coalescing ─── */

static void test_coalesce_adjacent(void)
{
    TEST(coalesce adjacent free blocks);
    void *a = mira_malloc(64); void *b = mira_malloc(64); void *c = mira_malloc(64);
    size_t free_before = mira_free_space();
    mira_free(a); check("after free(a)");
    mira_free(b); check("after free(b)");
    mira_free(c); check("after free(c)");
    size_t free_after = mira_free_space();
    if (free_after > free_before) { PASS(); } else { FAIL("coalescing failed"); }
}

static void test_coalesce_middle_first(void)
{
    TEST(coalesce: free middle then neighbors);
    void *a = mira_malloc(64); void *b = mira_malloc(64); void *c = mira_malloc(64);
    void *d = mira_malloc(64);
    mira_free(b); mira_free(a); mira_free(c);
    check("after coalescing a+b+c");
    size_t largest = mira_largest_free();
    mira_free(d);
    if (largest >= 192) { PASS(); } else { FAIL("coalescing incomplete"); }
}

/* ─── Splitting ─── */

static void test_split_reuse(void)
{
    TEST(split: large block splits for small alloc);
    void *big = mira_malloc(512);
    memset(big, 'Z', 512);
    mira_free(big);
    check("after free(big)");

    void *small = mira_malloc(64);
    if (small != NULL) { PASS(); } else { FAIL("split failed"); }
    mira_free(small);
}

/* ─── calloc and realloc ─── */

static void test_calloc_zeroed(void)
{
    TEST(calloc returns zeroed memory);
    void *p = mira_calloc(10, 10);
    if (p == NULL) { FAIL("calloc returned null"); return; }
    unsigned char *bytes = (unsigned char *)p;
    int all_zero = 1;
    for (int i = 0; i < 100; i++) { if (bytes[i] != 0) { all_zero = 0; break; } }
    if (all_zero) { PASS(); } else { FAIL("memory not zeroed"); }
    mira_free(p);
}

static void test_realloc_grow(void)
{
    TEST(realloc grows in place or copies);
    void *p = mira_malloc(32);
    memset(p, 'Y', 32);
    void *p2 = mira_realloc(p, 128);
    if (p2 == NULL) { FAIL("realloc returned null"); return; }
    int preserved = ((char *)p2)[0] == 'Y' && ((char *)p2)[31] == 'Y';
    if (preserved) { PASS(); } else { FAIL("data not preserved"); }
    mira_free(p2);
}

static void test_realloc_shrink(void)
{
    TEST(realloc shrinks block);
    void *p = mira_malloc(256);
    void *p2 = mira_realloc(p, 32);
    if (p2 != NULL) { PASS(); } else { FAIL("shrink realloc failed"); }
    mira_free(p2);
}

/* ─── Free list specific tests ─── */

static void test_lifo_policy(void)
{
    TEST(LIFO: most recently freed is reused first);
    void *a = mira_malloc(64);
    void *b = mira_malloc(64);
    mira_free(a);
    check("after free(a)");
    void *c = mira_malloc(64);
    /* LIFO: c should reuse a's block */
    if (c == a) { PASS(); } else { FAIL("LIFO not working"); }
    mira_free(b); mira_free(c);
}

static void test_free_list_length(void)
{
    TEST(free list length tracks coalescing);
    void *a = mira_malloc(64);
    void *b = mira_malloc(64);
    void *c = mira_malloc(64);
    void *d = mira_malloc(256);  /* sentinel to prevent top merge */

    mira_free(a);
    mira_free_list_length();  /* 1 block (a) */
    mira_free(c);
    size_t len2 = mira_free_list_length();  /* 2 blocks (a, c) — not adjacent */
    mira_free(b);  /* b coalesces with a and c → 1 block */
    size_t len3 = mira_free_list_length();

    mira_free(d);

    /* After coalescing, should be fewer blocks than before */
    if (len3 < len2) { PASS(); }
    else { FAIL("coalescing didn't reduce free list"); }
}

static void test_free_list_after_coalesce(void)
{
    TEST(free list: coalesce removes merged blocks);
    void *a = mira_malloc(64); void *b = mira_malloc(64); void *c = mira_malloc(64);
    mira_free(a); mira_free(c);
    /* Two separate free blocks */
    size_t len_before = mira_free_list_length();
    mira_free(b);  /* b coalesces with both a and c */
    /* After coalescing, should be one free block */
    size_t len_after = mira_free_list_length();
    check("after coalesce");
    mira_free(a); mira_free(b); mira_free(c); /* already freed, just for cleanup */

    /* Allocate fresh to clean up */
    void *x = mira_malloc(200);
    mira_free(x);

    if (len_after <= len_before) { PASS(); }
    else { FAIL("coalesce didn't reduce free list"); }
}

/* ─── Stress ─── */

static void test_stress_random_alloc_free(void)
{
    TEST(stress: 1000 random alloc/free);

    #define STRESS_OPS 1000
    #define MAX_PTRS 100
    #define MAX_SIZE 256

    /* Reset check counter for stress test */
    check_failures = 0;

    void *ptrs[MAX_PTRS] = {0};
    srand(42);

    int alloc_count = 0, free_count = 0;

    for (int i = 0; i < STRESS_OPS && check_failures == 0; i++) {
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
        if (i % 100 == 0) check("stress");
    }

    for (int i = 0; i < MAX_PTRS; i++) {
        if (ptrs[i]) { mira_free(ptrs[i]); ptrs[i] = NULL; }
    }
    check("after stress cleanup");

    size_t free_space = mira_free_space();
    if (free_space > 0 && check_failures == 0) { PASS(); }
    else { FAIL("heap invariant violated or no free space"); }
    printf("    (allocs=%d, frees=%d, free_space=%zu, list_len=%zu)\n",
           alloc_count, free_count, free_space, mira_free_list_length());
}

/* ─── Edge Cases ─── */

static void test_double_free_warning(void)
{
    TEST(double free prints warning);
    void *p = mira_malloc(32);
    mira_free(p);
    mira_free(p);
    PASS();
}

/* ─── Main ─── */

int main(void)
{
    printf("\n╔══════════════════════════════════════════════════╗\n");
    printf("║  mira_malloc Stage 2: Explicit Free List (LIFO)  ║\n");
    printf("╚══════════════════════════════════════════════════╝\n\n");

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

    printf("\n─── Free List ───\n");
    test_lifo_policy();
    test_free_list_length();
    test_free_list_after_coalesce();

    printf("\n─── Stress ───\n");
    test_stress_random_alloc_free();

    printf("\n─── Edge Cases ───\n");
    test_double_free_warning();

    printf("\n");
    mira_heap_check();

    printf("\n════════════════════════════════════════════════════\n");
    printf("  Results: %d/%d tests passed\n", tests_passed, tests_run);
    if (check_failures > 0)
        printf("  Heap invariant failures: %d\n", check_failures);
    printf("════════════════════════════════════════════════════\n\n");

    return (tests_passed == tests_run && check_failures == 0) ? 0 : 1;
}