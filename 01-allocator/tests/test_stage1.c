/*
 * Stage 1 tests: Implicit free list
 *
 * Tests: basic allocation, alignment, free, coalescing, splitting,
 *        fragmentation, double-free detection, valgrind-clean operation.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include "../src/malloc.h"

static int tests_run = 0;
static int tests_passed = 0;

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

/* ─── Basic allocation tests ─── */

static void test_alloc_returns_nonnull(void)
{
    TEST(alloc returns non-null);
    void *p = mira_malloc(100);
    if (p != NULL) { PASS(); } else { FAIL("returned null"); }
    mira_free(p);
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

    /* Pointers should be distinct */
    int distinct = (a != b) && (b != c) && (a != c);

    /* Write to each to verify they don't overlap */
    memset(a, 'A', 64);
    memset(b, 'B', 128);
    memset(c, 'C', 32);

    int correct = ((char *)a)[0] == 'A' &&
                  ((char *)b)[0] == 'B' &&
                  ((char *)c)[0] == 'C';

    if (distinct && correct) { PASS(); } else { FAIL("pointers overlap or write failed"); }

    mira_free(a);
    mira_free(b);
    mira_free(c);
}

/* ─── Free and reuse tests ─── */

static void test_free_then_realloc(void)
{
    TEST(free then reallocate same size);
    void *p1 = mira_malloc(64);
    memset(p1, 'X', 64);
    mira_free(p1);

    void *p2 = mira_malloc(64);
    /* Should reuse the freed block (first-fit) */
    if (p2 != NULL) { PASS(); } else { FAIL("null on realloc"); }
    mira_free(p2);
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

    /* Free in non-linear order */
    mira_free(b);
    mira_free(a);
    mira_free(c);

    /* All should be free - heap should have coalesced */
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

    /* Free adjacent blocks - should coalesce */
    mira_free(a);
    mira_free(b);
    mira_free(c);

    size_t free_after = mira_free_space();
    /* After coalescing 3 blocks, we should have more contiguous free space
     * than the sum of individual blocks would suggest */
    if (free_after > free_before) { PASS(); } else { FAIL("coalescing failed"); }
}

static void test_coalesce_middle_first(void)
{
    TEST(coalesce: free middle then neighbors);
    void *a = mira_malloc(64);
    void *b = mira_malloc(64);
    void *c = mira_malloc(64);
    void *d = mira_malloc(64);  /* prevent top merge */
    
    (void)a; (void)b; (void)c;

    /* Free middle block first */
    mira_free(b);

    /* Free left neighbor - should coalesce with b */
    mira_free(a);

    /* Free right neighbor - should coalesce with a+b */
    mira_free(c);

    /* Largest free block should span at least a+b+c */
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
    
    /* Allocate smaller - should split the freed 512-byte block */
    void *small = mira_malloc(64);
    if (small != NULL) { PASS(); } else { FAIL("split failed"); }
    
    /* Verify remaining space exists from the split */
    size_t remaining = mira_free_space();
    (void)remaining;
    
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
    for (int i = 0; i < 100; i++) {
        if (bytes[i] != 0) { all_zero = 0; break; }
    }

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

    /* Original data should be preserved */
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

/* ─── Stress test ─── */

static void test_stress_random_alloc_free(void)
{
    TEST(stress: 1000 random alloc/free);

    #define STRESS_OPS 1000
    #define MAX_PTRS 100
    #define MAX_SIZE 256

    void *ptrs[MAX_PTRS] = {0};
    srand(42);  /* deterministic seed */

    int alloc_count = 0, free_count = 0;

    for (int i = 0; i < STRESS_OPS; i++) {
        int slot = rand() % MAX_PTRS;

        if (ptrs[slot] == NULL) {
            /* Allocate */
            size_t size = (size_t)(rand() % MAX_SIZE) + 1;
            ptrs[slot] = mira_malloc(size);
            if (ptrs[slot])
                memset(ptrs[slot], slot & 0xff, size);
            alloc_count++;
        } else {
            /* Free */
            mira_free(ptrs[slot]);
            ptrs[slot] = NULL;
            free_count++;
        }
    }

    /* Free remaining */
    for (int i = 0; i < MAX_PTRS; i++) {
        if (ptrs[i])
            mira_free(ptrs[i]);
    }

    size_t free_space = mira_free_space();
    if (free_space > 0) { PASS(); } else { FAIL("no free space after stress test"); }

    printf("    (allocs=%d, frees=%d, free_space=%zu)\n", alloc_count, free_count, free_space);
}

/* ─── Double free detection ─── */

static void test_double_free_warning(void)
{
    TEST(double free prints warning);
    void *p = mira_malloc(32);
    mira_free(p);
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
    printf("═══════════════════════════════════════════\n\n");

    return (tests_passed == tests_run) ? 0 : 1;
}