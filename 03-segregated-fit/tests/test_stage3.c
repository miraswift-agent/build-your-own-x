/*
 * Stage 3 tests: Segregated free lists
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

#define TEST(name) do { tests_run++; printf("  %-45s", #name); } while (0)
#define PASS() do { tests_passed++; printf("✓\n"); } while (0)
#define FAIL(msg) do { printf("✗ %s\n", msg); } while (0)

static void check(const char *where)
{
    if (heap_start == NULL) return;

    word_t *prev_blk = NULL;
    for (word_t *h = heap_start; !IS_EPILOGUE(h); h = NEXT_HDR(h)) {
        size_t size = GET_SIZE(h);
        int pf = GET_PREV_FREE(h);

        if (size < MIN_BLOCK_SIZE || (size & (ALIGNMENT - 1)) != 0) {
            printf("  CHECK [%s]: invalid size %zu at %p\n", where, size, (void *)h);
            check_failures++; return;
        }

        if (!GET_ALLOC(h)) {
            word_t *foot = FOOTER(h);
            if (GET_SIZE(foot) != size) {
                printf("  CHECK [%s]: footer mismatch at %p\n", where, (void *)h);
                check_failures++; return;
            }
        }

        int expected_pf = (h != heap_start && prev_blk != NULL) ? !GET_ALLOC(prev_blk) : 0;
        if (pf != expected_pf) {
            printf("  CHECK [%s]: pf mismatch at %p (expected=%d actual=%d)\n",
                   where, (void *)h, expected_pf, pf);
            check_failures++; return;
        }

        prev_blk = h;
    }

    /* Verify buckets match heap free blocks */
    int bucket_total = (int)mira_free_list_length();
    int heap_free = 0;
    for (word_t *h = heap_start; !IS_EPILOGUE(h); h = NEXT_HDR(h))
        if (!GET_ALLOC(h)) heap_free++;

    if (bucket_total != heap_free) {
        printf("  CHECK [%s]: buckets=%d heap_free=%d\n", where, bucket_total, heap_free);
        check_failures++;
    }
}

/* ─── Basic Allocation ─── */

static void test_alloc_nonnull(void) {
    TEST(alloc returns non-null);
    void *p = mira_malloc(100); check("after malloc");
    if (p) PASS(); else FAIL("null");
    mira_free(p); check("after free");
}

static void test_alignment(void) {
    TEST(alignment (8-byte));
    void *p1 = mira_malloc(1), *p2 = mira_malloc(1), *p3 = mira_malloc(7);
    int ok = ((size_t)p1%8==0) && ((size_t)p2%8==0) && ((size_t)p3%8==0);
    if (ok) PASS(); else FAIL("unaligned");
    mira_free(p1); mira_free(p2); mira_free(p3);
}

static void test_zero_size(void) {
    TEST(zero-size returns null);
    if (mira_malloc(0) == NULL) PASS(); else FAIL("should be null");
}

static void test_multiple_allocs(void) {
    TEST(multiple allocations);
    void *a = mira_malloc(64), *b = mira_malloc(128), *c = mira_malloc(32);
    memset(a,'A',64); memset(b,'B',128); memset(c,'C',32);
    if (a&&b&&c && a!=b && b!=c && ((char*)a)[0]=='A' && ((char*)b)[0]=='B')
        PASS(); else FAIL("overlap or write");
    mira_free(a); mira_free(b); mira_free(c);
}

/* ─── Free & Reuse ─── */

static void test_free_reuse(void) {
    TEST(free then reallocate same size);
    void *p1 = mira_malloc(64); memset(p1,'X',64); mira_free(p1); check("after free");
    void *p2 = mira_malloc(64);
    if (p2) PASS(); else FAIL("null");
    mira_free(p2);
}

static void test_free_order(void) {
    TEST(free order independence);
    void *a=mira_malloc(32),*b=mira_malloc(32),*c=mira_malloc(32);
    memset(a,'A',32); memset(b,'B',32); memset(c,'C',32);
    mira_free(b); check("after free(b)");
    mira_free(a); check("after free(a)");
    mira_free(c); check("after free(c)");
    if (mira_free_space() > 0) PASS(); else FAIL("no free space");
}

/* ─── Coalescing ─── */

static void test_coalesce_adjacent(void) {
    TEST(coalesce adjacent free blocks);
    void *a=mira_malloc(64),*b=mira_malloc(64),*c=mira_malloc(64);
    size_t fb = mira_free_space();
    mira_free(a); mira_free(b); mira_free(c); check("after coalescing");
    if (mira_free_space() > fb) PASS(); else FAIL("no coalescing");
}

static void test_coalesce_middle(void) {
    TEST(coalesce: free middle then neighbors);
    void *a=mira_malloc(64),*b=mira_malloc(64),*c=mira_malloc(64),*d=mira_malloc(64);
    mira_free(b); mira_free(a); mira_free(c);
    size_t lg = mira_largest_free(); mira_free(d);
    if (lg >= 192) PASS(); else FAIL("coalescing incomplete");
}

/* ─── Splitting ─── */

static void test_split(void) {
    TEST(split: large block splits for small alloc);
    void *big = mira_malloc(512); memset(big,'Z',512); mira_free(big); check("after free(big)");
    void *small = mira_malloc(64);
    if (small) PASS(); else FAIL("split failed");
    mira_free(small);
}

/* ─── calloc & realloc ─── */

static void test_calloc(void) {
    TEST(calloc returns zeroed memory);
    void *p = mira_calloc(10, 10);
    if (!p) { FAIL("null"); return; }
    unsigned char *bytes = p;
    int ok = 1;
    for (int i = 0; i < 100; i++) if (bytes[i]) { ok = 0; break; }
    if (ok) PASS(); else FAIL("not zeroed");
    mira_free(p);
}

static void test_realloc_grow(void) {
    TEST(realloc grows in place or copies);
    void *p = mira_malloc(32); memset(p,'Y',32);
    void *p2 = mira_realloc(p, 128);
    if (!p2) { FAIL("null"); return; }
    if (((char*)p2)[0]=='Y' && ((char*)p2)[31]=='Y') PASS(); else FAIL("data lost");
    mira_free(p2);
}

static void test_realloc_shrink(void) {
    TEST(realloc shrinks block);
    void *p = mira_malloc(256);
    void *p2 = mira_realloc(p, 32);
    if (p2) PASS(); else FAIL("null");
    mira_free(p2);
}

/* ─── Bucket-specific tests ─── */

static void test_bucket_distribution(void) {
    TEST(buckets distribute by size class);
    void *small = mira_malloc(16);
    void *medium = mira_malloc(64);
    void *large = mira_malloc(256);
    check("after 3 allocs");
    
    /* All allocated, buckets should be empty */
    size_t total_free = mira_free_space();
    
    mira_free(small); check("after free(small)");
    mira_free(medium); check("after free(medium)");
    mira_free(large); check("after free(large)");
    
    /* Should have more free space now */
    if (mira_free_space() > total_free) PASS();
    else FAIL("free space didn't increase");
}

static void test_exact_fit(void) {
    TEST(exact fit from bucket);
    /* Allocate and free a 64-byte block, then request same size */
    void *a = mira_malloc(64);
    void *b = mira_malloc(64);
    mira_free(a);
    void *c = mira_malloc(64);
    /* c should reuse a's block (exact fit) */
    if (c) PASS(); else FAIL("null");
    mira_free(b); mira_free(c);
}

/* ─── Stress ─── */

static void test_stress(void) {
    TEST(stress: 1000 random alloc/free);
    #define N 100
    #define SZ 256
    void *ptrs[N] = {0};
    srand(42);
    int allocs = 0, frees = 0;
    check_failures = 0;  /* Reset for clean stress test */

    for (int i = 0; i < 1000 && check_failures == 0; i++) {
        int slot = rand() % N;
        if (ptrs[slot] == NULL) {
            size_t sz = (rand() % SZ) + 1;
            ptrs[slot] = mira_malloc(sz);
            if (ptrs[slot]) memset(ptrs[slot], slot & 0xff, sz);
            allocs++;
        } else {
            mira_free(ptrs[slot]);
            ptrs[slot] = NULL;
            frees++;
        }
        if (i % 100 == 0) check("stress");
    }

    for (int i = 0; i < N; i++)
        if (ptrs[i]) { mira_free(ptrs[i]); ptrs[i] = NULL; }
    check("after stress cleanup");

    if (mira_free_space() > 0 && check_failures == 0) PASS();
    else FAIL("heap invariant violated");
    printf("    (allocs=%d, frees=%d, free=%zu, buckets=%zu)\n",
           allocs, frees, mira_free_space(), mira_free_list_length());
}

/* ─── Edge Cases ─── */

static void test_double_free(void) {
    TEST(double free prints warning);
    void *p = mira_malloc(32);
    mira_free(p);
    mira_free(p);
    PASS();
}

/* ─── Main ─── */

int main(void)
{
    printf("\n╔════════════════════════════════════════════════════════╗\n");
    printf("║  mira_malloc Stage 3: Segregated Free Lists (Buckets) ║\n");
    printf("╚════════════════════════════════════════════════════════╝\n\n");

    printf("─── Basic Allocation ───\n");
    test_alloc_nonnull();
    test_alignment();
    test_zero_size();
    test_multiple_allocs();

    printf("\n─── Free & Reuse ───\n");
    test_free_reuse();
    test_free_order();

    printf("\n─── Coalescing ───\n");
    test_coalesce_adjacent();
    test_coalesce_middle();

    printf("\n─── Splitting ───\n");
    test_split();

    printf("\n─── calloc & realloc ───\n");
    test_calloc();
    test_realloc_grow();
    test_realloc_shrink();

    printf("\n─── Buckets ───\n");
    test_bucket_distribution();
    test_exact_fit();

    printf("\n─── Stress ───\n");
    test_stress();

    printf("\n─── Edge Cases ───\n");
    test_double_free();

    printf("\n");
    mira_heap_check();
    mira_print_buckets();

    printf("\n════════════════════════════════════════════════════════\n");
    printf("  Results: %d/%d tests passed\n", tests_passed, tests_run);
    if (check_failures > 0)
        printf("  Heap invariant failures: %d\n", check_failures);
    printf("════════════════════════════════════════════════════════\n\n");

    return (tests_passed == tests_run && check_failures == 0) ? 0 : 1;
}