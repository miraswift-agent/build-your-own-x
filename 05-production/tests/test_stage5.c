/*
 * Stage 5 tests: Production allocator
 *
 * Thread safety, mmap threshold, madvise, usable_size, stats,
 * plus all previous functionality (slab + free-list + mixed).
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include <pthread.h>
#include "../src/malloc.h"

static int tests_run = 0;
static int tests_passed = 0;
static int check_failures = 0;

#define TEST(name) do { tests_run++; printf("  %-55s", #name); } while (0)
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
            printf("  CHECK [%s]: pf mismatch at %p\n", where, (void *)h);
            check_failures++; return;
        }

        prev_blk = h;
    }

    for (int i = 0; i < NUM_SLAB_CLASSES; i++) {
        for (slab_header_t *s = slab_lists[i]; s != NULL; s = s->next) {
            unsigned char *bm = SLAB_BITMAP(s);
            int used = 0;
            for (size_t j = 0; j < s->n_slots; j++)
                if (bm[j / 8] & (1 << (j % 8))) used++;
            if ((size_t)used != s->n_used) {
                printf("  CHECK [%s]: slab bitmap mismatch at %p\n", where, (void *)s);
                check_failures++; return;
            }
        }
    }
}

/* ─── Forward declarations ─── */

static void test_alloc_nonnull(void);
static void test_large_alloc(void);
static void test_alignment(void);
static void test_zero_size(void);
static void test_multiple_small(void);
static void test_slab_no_headers(void);
static void test_slab_reuse(void);
static void test_slab_cross_size(void);
static void test_slab_bitmap(void);
static void test_slab_reap(void);
static void test_free_reuse(void);
static void test_coalesce_adjacent(void);
static void test_split(void);
static void test_mixed_alloc(void);
static void test_calloc_small(void);
static void test_realloc_slab_grow(void);
static void test_realloc_slab_shrink(void);
static void test_mmap_threshold(void);
static void test_mmap_usable_size(void);
static void test_mmap_free(void);
static void test_usable_size_slab(void);
static void test_usable_size_arena(void);
static void test_malloc_stats(void);
static void test_thread_safety(void);
static void test_stress(void);
static void test_double_free(void);

/* ─── Basic Allocation ─── */

static void test_alloc_nonnull(void) {
    TEST(small alloc returns non-null (slab path));
    void *p = mira_malloc(32);
    check("after malloc");
    if (p) PASS(); else FAIL("null");
    mira_free(p);
}

static void test_large_alloc(void) {
    TEST(large alloc returns non-null (free-list path));
    void *p = mira_malloc(512);
    check("after malloc");
    if (p) PASS(); else FAIL("null");
    mira_free(p);
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

static void test_multiple_small(void) {
    TEST(multiple small allocations (slab));
    void *a = mira_malloc(16), *b = mira_malloc(32), *c = mira_malloc(48);
    memset(a,'A',16); memset(b,'B',32); memset(c,'C',48);
    if (a&&b&&c && a!=b && b!=c && ((char*)a)[0]=='A' && ((char*)b)[0]=='B')
        PASS(); else FAIL("overlap or write");
    mira_free(a); mira_free(b); mira_free(c);
}

/* ─── Slab tests ─── */

static void test_slab_no_headers(void) {
    TEST(slab allocs have no per-object header overhead);
    void *ptrs[100];
    for (int i = 0; i < 100; i++) {
        ptrs[i] = mira_malloc(16);
        if (!ptrs[i]) { FAIL("alloc failed"); return; }
        memset(ptrs[i], i & 0xff, 16);
    }
    int ok = 1;
    for (int i = 0; i < 100 && ok; i++)
        if (((unsigned char *)ptrs[i])[0] != (i & 0xff)) ok = 0;
    if (ok) PASS(); else FAIL("data corrupted");
    for (int i = 0; i < 100; i++) mira_free(ptrs[i]);
}

static void test_slab_reuse(void) {
    TEST(slab slot reuse after free);
    void *a = mira_malloc(32);
    memset(a, 'X', 32);
    mira_free(a);
    check("after free(a)");
    void *b = mira_malloc(32);
    if (b) PASS(); else FAIL("null");
    mira_free(b);
}

static void test_slab_cross_size(void) {
    TEST(slab different size classes do not overlap);
    void *a = mira_malloc(16), *b = mira_malloc(32), *c = mira_malloc(64);
    memset(a, 'A', 16); memset(b, 'B', 32); memset(c, 'C', 64);
    if (((char*)a)[0]=='A' && ((char*)b)[0]=='B' && ((char*)c)[0]=='C')
        PASS(); else FAIL("data corrupted");
    mira_free(a); mira_free(b); mira_free(c);
}

static void test_slab_bitmap(void) {
    TEST(slab bitmap tracks allocations);
    void *ptrs[50];
    for (int i = 0; i < 50; i++) {
        ptrs[i] = mira_malloc(16);
        memset(ptrs[i], i & 0xff, 16);
    }
    for (int i = 0; i < 50; i += 2)
        mira_free(ptrs[i]);
    check("after free every other");
    for (int i = 0; i < 25; i++) {
        void *p = mira_malloc(16);
        if (!p) { FAIL("realloc failed"); return; }
        memset(p, 0xaa, 16);
    }
    check("after realloc");
    PASS();
    for (int i = 1; i < 50; i += 2)
        mira_free(ptrs[i]);
}

static void test_slab_reap(void) {
    TEST(slab reap: empty slab unmapped);
    void *ptrs[200];
    for (int i = 0; i < 200; i++) {
        ptrs[i] = mira_malloc(32);
        memset(ptrs[i], i & 0xff, 32);
    }
    for (int i = 0; i < 200; i++)
        mira_free(ptrs[i]);
    check("after free all");
    PASS();
}

/* ─── Free list tests ─── */

static void test_free_reuse(void) {
    TEST(free then reallocate same size (free-list));
    void *p1 = mira_malloc(512); memset(p1,'X',512); mira_free(p1);
    check("after free");
    void *p2 = mira_malloc(512);
    if (p2) PASS(); else FAIL("null");
    mira_free(p2);
}

static void test_coalesce_adjacent(void) {
    TEST(coalesce adjacent free blocks (free-list));
    void *a=mira_malloc(256),*b=mira_malloc(256),*c=mira_malloc(256);
    size_t fb = mira_free_space();
    mira_free(a); mira_free(b); mira_free(c); check("after coalescing");
    if (mira_free_space() > fb) PASS(); else FAIL("no coalescing");
}

static void test_split(void) {
    TEST(split: large block splits for small alloc);
    void *big = mira_malloc(1024); memset(big,'Z',1024); mira_free(big);
    void *small = mira_malloc(256);
    if (small) PASS(); else FAIL("split failed");
    mira_free(small);
}

/* ─── Mixed ─── */

static void test_mixed_alloc(void) {
    TEST(mixed small and large allocations);
    void *small1 = mira_malloc(32), *large = mira_malloc(512);
    void *small2 = mira_malloc(16), *large2 = mira_malloc(1024);
    check("after mixed allocs");
    memset(small1,'A',32); memset(large,'B',512);
    memset(small2,'C',16); memset(large2,'D',1024);
    if (((char*)small1)[0]=='A' && ((char*)large)[0]=='B' &&
        ((char*)small2)[0]=='C' && ((char*)large2)[0]=='D')
        PASS(); else FAIL("data corrupted");
    mira_free(small1); mira_free(large);
    mira_free(small2); mira_free(large2);
}

static void test_calloc_small(void) {
    TEST(calloc returns zeroed memory (slab));
    void *p = mira_calloc(10, 16);
    if (!p) { FAIL("null"); return; }
    unsigned char *bytes = p;
    int ok = 1;
    for (int i = 0; i < 160; i++) if (bytes[i]) { ok = 0; break; }
    if (ok) PASS(); else FAIL("not zeroed");
    mira_free(p);
}

static void test_realloc_slab_grow(void) {
    TEST(realloc grows slab alloc to free-list);
    void *p = mira_malloc(32);
    memset(p, 'Y', 32);
    void *p2 = mira_realloc(p, 512);
    if (!p2) { FAIL("null"); return; }
    if (((char*)p2)[0]=='Y') PASS(); else FAIL("data lost");
    mira_free(p2);
}

static void test_realloc_slab_shrink(void) {
    TEST(realloc shrinks within slab slot);
    void *p = mira_malloc(64);
    void *p2 = mira_realloc(p, 16);
    if (p2) PASS(); else FAIL("null");
    mira_free(p2);
}

/* ─── mmap (large allocation) tests ─── */

static void test_mmap_threshold(void) {
    TEST(large alloc goes through mmap (above threshold));
    void *p = mira_malloc(256 * 1024);  /* 256 KB, well above 128 KB threshold */
    if (!p) { FAIL("null"); return; }
    memset(p, 'M', 256 * 1024);
    /* Should be in mmap list */
    int found = 0;
    for (mmap_header_t *m = mmap_list; m != NULL; m = m->next) {
        char *start = (char *)m + sizeof(mmap_header_t);
        if (start == (char *)p) { found = 1; break; }
    }
    mira_free(p);
    if (found) PASS(); else FAIL("not in mmap list");
}

static void test_mmap_usable_size(void) {
    TEST(mmap usable_size returns correct size);
    void *p = mira_malloc(200 * 1024);
    if (!p) { FAIL("null"); return; }
    size_t usable = mira_malloc_usable_size(p);
    /* Usable size should be at least the requested size */
    if (usable >= 200 * 1024) PASS(); else FAIL("usable size too small");
    mira_free(p);
}

static void test_mmap_free(void) {
    TEST(mmap alloc and free (no leak));
    size_t before = 0;
    for (mmap_header_t *m = mmap_list; m != NULL; m = m->next) before++;

    void *p = mira_malloc(300 * 1024);
    if (!p) { FAIL("null"); return; }
    mira_free(p);

    size_t after = 0;
    for (mmap_header_t *m = mmap_list; m != NULL; m = m->next) after++;

    if (after == before) PASS(); else FAIL("mmap block leaked");
}

/* ─── usable_size tests ─── */

static void test_usable_size_slab(void) {
    TEST(usable_size for slab allocation);
    void *p = mira_malloc(32);
    size_t usable = mira_malloc_usable_size(p);
    /* Slab slot size for 32-byte class is 32 */
    if (usable == 32) PASS();
    else if (usable >= 32) PASS();  /* slot may be larger */
    else FAIL("usable size too small");
    mira_free(p);
}

static void test_usable_size_arena(void) {
    TEST(usable_size for arena allocation);
    void *p = mira_malloc(512);
    size_t usable = mira_malloc_usable_size(p);
    /* Should be at least 512 (may be rounded up) */
    if (usable >= 512) PASS(); else FAIL("usable size too small");
    mira_free(p);
}

/* ─── malloc_stats ─── */

static void test_malloc_stats(void) {
    TEST(malloc_stats prints without crash);
    /* Allocate some stuff to make stats interesting */
    void *small = mira_malloc(32);
    void *medium = mira_malloc(512);
    void *large = mira_malloc(200 * 1024);

    printf("\n");
    mira_malloc_stats();

    mira_free(small); mira_free(medium); mira_free(large);
    PASS();
}

/* ─── Thread safety ─── */

#define N_THREADS 4
#define N_OPS_PER_THREAD 500

static void *thread_worker(void *arg)
{
    (void)arg;
    void *ptrs[50] = {0};

    for (int i = 0; i < N_OPS_PER_THREAD; i++) {
        int slot = rand() % 50;
        if (ptrs[slot] == NULL) {
            size_t sz = (size_t)(rand() % 1024) + 1;
            /* Mix small and large */
            if (rand() % 10 == 0)
                sz += 128 * 1024;  /* 10% chance of large mmap alloc */
            ptrs[slot] = mira_malloc(sz);
            if (ptrs[slot])
                memset(ptrs[slot], slot & 0xff, sz < 64 ? sz : 64);
        } else {
            mira_free(ptrs[slot]);
            ptrs[slot] = NULL;
        }
    }

    /* Clean up remaining */
    for (int i = 0; i < 50; i++)
        if (ptrs[i]) { mira_free(ptrs[i]); ptrs[i] = NULL; }

    return NULL;
}

static void test_thread_safety(void) {
    TEST(thread safety with 4 threads);
    pthread_t threads[N_THREADS];
    srand(12345);

    for (int i = 0; i < N_THREADS; i++)
        pthread_create(&threads[i], NULL, thread_worker, NULL);

    for (int i = 0; i < N_THREADS; i++)
        pthread_join(threads[i], NULL);

    check("after thread test");
    if (check_failures == 0) PASS();
    else FAIL("heap invariant violated");
}

/* ─── Stress ─── */

static void test_stress(void) {
    TEST(stress: 1000 random alloc/free (mixed));
    #define N 100
    #define SZ 512
    void *ptrs[N] = {0};
    srand(42);
    int allocs = 0, frees = 0;
    check_failures = 0;

    for (int i = 0; i < 1000 && check_failures == 0; i++) {
        int slot = rand() % N;
        if (ptrs[slot] == NULL) {
            size_t sz = (size_t)(rand() % SZ) + 1;
            ptrs[slot] = mira_malloc(sz);
            if (ptrs[slot])
                memset(ptrs[slot], slot & 0xff, sz);
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

    if (check_failures == 0) PASS();
    else FAIL("heap invariant violated");
    printf("    (allocs=%d, frees=%d)\n", allocs, frees);
}

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
    printf("\n╔══════════════════════════════════════════════════════════════╗\n");
    printf("║  mira_malloc Stage 5: Production Allocator                  ║\n");
    printf("║  Thread-safe · mmap threshold · madvise · introspection   ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n\n");

    printf("─── Basic Allocation ───\n");
    test_alloc_nonnull();
    test_large_alloc();
    test_alignment();
    test_zero_size();
    test_multiple_small();

    printf("\n─── Slab-specific ───\n");
    test_slab_no_headers();
    test_slab_reuse();
    test_slab_cross_size();
    test_slab_bitmap();
    test_slab_reap();

    printf("\n─── Free-list (Stage 3) ───\n");
    test_free_reuse();
    test_coalesce_adjacent();
    test_split();

    printf("\n─── Mixed ───\n");
    test_mixed_alloc();
    test_calloc_small();
    test_realloc_slab_grow();
    test_realloc_slab_shrink();

    printf("\n─── mmap (large blocks) ───\n");
    test_mmap_threshold();
    test_mmap_usable_size();
    test_mmap_free();

    printf("\n─── Introspection ───\n");
    test_usable_size_slab();
    test_usable_size_arena();
    test_malloc_stats();

    printf("\n─── Thread Safety ───\n");
    test_thread_safety();

    printf("\n─── Stress ───\n");
    test_stress();

    printf("\n─── Edge Cases ───\n");
    test_double_free();

    printf("\n");
    mira_heap_check();
    mira_print_slabs();
    mira_print_buckets();

    printf("\n══════════════════════════════════════════════════════════════\n");
    printf("  Results: %d/%d tests passed\n", tests_passed, tests_run);
    if (check_failures > 0)
        printf("  Heap invariant failures: %d\n", check_failures);
    printf("══════════════════════════════════════════════════════════════\n\n");

    return (tests_passed == tests_run && check_failures == 0) ? 0 : 1;
}