/*
 * mira_malloc — Stage 2: Explicit free list with boundary tags,
 *               prev_free bit, singly-linked free list, mmap heap.
 *
 * Improvements over Stage 1:
 * - Free blocks linked via next_free pointer in payload area
 * - Singly-linked free list (O(n) removal, but simpler and no stale prev ptrs)
 * - Allocation scans only free blocks, not all blocks
 * - LIFO insertion policy
 */

#define _GNU_SOURCE
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <sys/mman.h>
#include "malloc.h"

/* ─── Global state ─── */

word_t *heap_start = NULL;
word_t *heap_end   = NULL;

/* Singly-linked free list (LIFO). Only uses next_free pointer.
 * Removal is O(n) but eliminates stale prev pointer issues. */
static word_t *free_list_head = NULL;

#define HEAP_INIT_SIZE  (1 << 20)
#define HEAP_GROW_SIZE  (1 << 16)

static word_t *heap_base = NULL;
static size_t  heap_capacity = 0;
static size_t  heap_used = 0;

/* ─── Inline helpers ─── */

static inline void set_header(word_t *h, size_t size, int alloc, int prev_free)
{
    *(h) = size | (alloc & 0x1) | ((prev_free & 0x1) << 1);
}

static inline void set_free_block(word_t *h, size_t size, int prev_free)
{
    set_header(h, size, 0, prev_free);
    word_t *foot = FOOTER(h);
    *foot = size;  /* footer: size | alloc=0 */
}

static inline void set_alloc_block(word_t *h, size_t size, int prev_free)
{
    set_header(h, size, 1, prev_free);
    /* No footer — payload uses that space */
}

/* ─── Free list operations (singly-linked) ─── */

static inline void list_insert(word_t *h)
{
    FREE_NEXT(h) = free_list_head;
    free_list_head = h;
}

static void list_remove(word_t *h)
{
    if (free_list_head == h) {
        free_list_head = FREE_NEXT(h);
        FREE_NEXT(h) = NULL;
        return;
    }
    for (word_t *prev = free_list_head; prev != NULL; prev = FREE_NEXT(prev)) {
        if (FREE_NEXT(prev) == h) {
            FREE_NEXT(prev) = FREE_NEXT(h);
            FREE_NEXT(h) = NULL;
            return;
        }
    }
    /* Block not in free list — may have been removed by coalesce already */
}

/* ─── Internal helpers ─── */

static word_t *find_fit(size_t size)
{
    for (word_t *h = free_list_head; h != NULL; h = FREE_NEXT(h)) {
        if (!GET_ALLOC(h) && GET_SIZE(h) >= size)
            return h;
    }
    return NULL;
}

static word_t *coalesce(word_t *h);
static word_t *extend_heap(size_t size);

static void split_block(word_t *h, size_t needed)
{
    size_t block_size = GET_SIZE(h);
    size_t remainder = block_size - needed;

    if (remainder >= MIN_BLOCK_SIZE) {
        word_t *rest = (word_t *)((char *)h + needed);
        set_free_block(rest, remainder, 0);
        list_insert(rest);

        set_alloc_block(h, needed, GET_PREV_FREE(h));

        word_t *after = NEXT_HDR(rest);
        if (!IS_EPILOGUE(after))
            set_header(after, GET_SIZE(after), GET_ALLOC(after), 1);
        else
            set_header(after, 0, 1, 1);
    }
}

static word_t *coalesce(word_t *h)
{
    size_t size = GET_SIZE(h);
    int prev_free_bit = GET_PREV_FREE(h);

    word_t *next = NEXT_HDR(h);
    int next_free = !IS_EPILOGUE(next) && !GET_ALLOC(next);

    if (prev_free_bit && next_free) {
        word_t *prev = (word_t *)((char *)h - *(word_t *)((char *)h - WORD_SIZE));
        list_remove(prev);
        list_remove(next);

        size += GET_SIZE(prev) + GET_SIZE(next);
        int prev_prev_free = GET_PREV_FREE(prev);
        set_free_block(prev, size, prev_prev_free);

        word_t *after = NEXT_HDR(prev);
        if (!IS_EPILOGUE(after))
            set_header(after, GET_SIZE(after), GET_ALLOC(after), 1);
        else
            set_header(after, 0, 1, 1);

        h = prev;
    } else if (prev_free_bit) {
        word_t *prev = (word_t *)((char *)h - *(word_t *)((char *)h - WORD_SIZE));
        list_remove(prev);

        size += GET_SIZE(prev);
        int prev_prev_free = GET_PREV_FREE(prev);
        set_free_block(prev, size, prev_prev_free);

        set_header(next, GET_SIZE(next), GET_ALLOC(next), 1);
        h = prev;
    } else if (next_free) {
        list_remove(next);

        size += GET_SIZE(next);
        set_free_block(h, size, 0);

        word_t *after = NEXT_HDR(h);
        if (!IS_EPILOGUE(after))
            set_header(after, GET_SIZE(after), GET_ALLOC(after), 1);
        else
            set_header(after, 0, 1, 1);
    } else {
        set_free_block(h, size, 0);
        set_header(next, GET_SIZE(next), GET_ALLOC(next), 1);
    }

    list_insert(h);
    return h;
}

static word_t *extend_heap(size_t size)
{
    size = ALIGN(size);
    size_t needed = size + WORD_SIZE;

    if (heap_used + needed > heap_capacity) {
        size_t grow = HEAP_GROW_SIZE;
        if (grow < needed) grow = needed;
        size_t new_capacity = heap_capacity + grow;

        void *new_base = mremap(heap_base, heap_capacity, new_capacity, MREMAP_MAYMOVE);
        if (new_base == MAP_FAILED) {
            new_base = mmap(NULL, new_capacity, PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            if (new_base == MAP_FAILED) return NULL;
            memcpy(new_base, heap_base, heap_used);
            munmap(heap_base, heap_capacity);

            ptrdiff_t offset = (char *)new_base - (char *)heap_base;
            if (offset != 0) {
                free_list_head = (word_t *)((char *)free_list_head + offset);
                heap_start = (word_t *)((char *)heap_start + offset);
                heap_end = (word_t *)((char *)heap_end + offset);
                for (word_t *f = free_list_head; f != NULL; f = FREE_NEXT(f)) {
                    if (FREE_NEXT(f) != NULL)
                        FREE_NEXT(f) = (word_t *)((char *)FREE_NEXT(f) + offset);
                }
            }
            heap_base = (word_t *)new_base;
        } else {
            heap_base = (word_t *)new_base;
        }
        heap_capacity = new_capacity;
    }

    word_t *prev = NULL;
    for (word_t *p = heap_start; !IS_EPILOGUE(p); p = NEXT_HDR(p)) {
        /* Sanity check: block size must be valid */
        if (GET_SIZE(p) < MIN_BLOCK_SIZE || (GET_SIZE(p) & (ALIGNMENT - 1)) != 0) {
            fprintf(stderr, "mira_malloc: heap corruption at %p (size=%zu)\n", (void *)p, GET_SIZE(p));
            return NULL;
        }
        prev = p;
    }

    if (prev != NULL && !GET_ALLOC(prev)) {
        size_t merged = GET_SIZE(prev) + size + WORD_SIZE;
        int prev_prev_free = GET_PREV_FREE(prev);
        list_remove(prev);
        set_free_block(prev, merged, prev_prev_free);

        word_t *epi = NEXT_HDR(prev);
        set_header(epi, 0, 1, 1);
        heap_end = epi;
        heap_used += needed;
        list_insert(prev);
        return prev;
    }

    word_t *h = heap_end;
    int prev_free_val = (prev != NULL) ? !GET_ALLOC(prev) : 0;
    set_free_block(h, size, prev_free_val);

    word_t *epi = NEXT_HDR(h);
    set_header(epi, 0, 1, 1);
    heap_end = epi;
    heap_used += needed;
    list_insert(h);
    return h;
}

/* ─── Public API ─── */

void *mira_malloc(size_t size)
{
    if (size == 0) return NULL;

    size_t needed = ALIGN(WORD_SIZE + size);
    if (needed < MIN_BLOCK_SIZE) needed = MIN_BLOCK_SIZE;

    if (heap_start == NULL) {
        heap_base = mmap(NULL, HEAP_INIT_SIZE, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (heap_base == MAP_FAILED) return NULL;
        heap_capacity = HEAP_INIT_SIZE;

        word_t *prologue = heap_base;
        set_alloc_block(prologue, MIN_BLOCK_SIZE, 0);
        word_t *pro_foot = FOOTER(prologue);
        *pro_foot = MIN_BLOCK_SIZE | 0x1;

        word_t *epilogue = NEXT_HDR(prologue);
        set_header(epilogue, 0, 1, 0);

        heap_start = prologue;
        heap_end = epilogue;
        heap_used = MIN_BLOCK_SIZE + WORD_SIZE;
    }

    word_t *h = find_fit(needed);

    if (h != NULL) {
        list_remove(h);
        split_block(h, needed);
        set_alloc_block(h, needed, GET_PREV_FREE(h));
        word_t *next = NEXT_HDR(h);
        if (!IS_EPILOGUE(next))
            set_header(next, GET_SIZE(next), GET_ALLOC(next), 0);
    } else {
        h = extend_heap(needed);
        if (h == NULL) return NULL;
        list_remove(h);
        split_block(h, needed);
        set_alloc_block(h, needed, GET_PREV_FREE(h));
        word_t *next = NEXT_HDR(h);
        if (!IS_EPILOGUE(next))
            set_header(next, GET_SIZE(next), GET_ALLOC(next), 0);
    }

    return HDR_TO_PAYLOAD(h);
}

void mira_free(void *ptr)
{
    if (ptr == NULL) return;

    word_t *h = PAYLOAD_TO_HDR(ptr);
    if (!GET_ALLOC(h)) {
        fprintf(stderr, "mira_free: double free detected at %p\n", ptr);
        return;
    }

    coalesce(h);
}

void *mira_calloc(size_t nmemb, size_t size)
{
    if (nmemb == 0 || size == 0) return NULL;

    size_t total = nmemb * size;
    if (nmemb != 0 && total / nmemb != size) return NULL;

    void *ptr = mira_malloc(total);
    if (ptr) memset(ptr, 0, total);
    return ptr;
}

void *mira_realloc(void *ptr, size_t size)
{
    if (ptr == NULL) return mira_malloc(size);
    if (size == 0) { mira_free(ptr); return NULL; }

    word_t *h = PAYLOAD_TO_HDR(ptr);
    size_t old_size = GET_SIZE(h);
    size_t needed = ALIGN(WORD_SIZE + size);
    if (needed < MIN_BLOCK_SIZE) needed = MIN_BLOCK_SIZE;

    if (old_size >= needed) {
        size_t remainder = old_size - needed;
        if (remainder >= MIN_BLOCK_SIZE) {
            word_t *rest = (word_t *)((char *)h + needed);
            word_t *next = NEXT_HDR(h);

            if (!IS_EPILOGUE(next) && !GET_ALLOC(next)) {
                list_remove(next);
                size_t merged = remainder + GET_SIZE(next);
                set_free_block(rest, merged, 0);
                list_insert(rest);
                word_t *after = NEXT_HDR(rest);
                if (!IS_EPILOGUE(after))
                    set_header(after, GET_SIZE(after), GET_ALLOC(after), 1);
                else
                    set_header(after, 0, 1, 1);
            } else {
                set_free_block(rest, remainder, 0);
                list_insert(rest);
                word_t *after = NEXT_HDR(rest);
                if (!IS_EPILOGUE(after))
                    set_header(after, GET_SIZE(after), GET_ALLOC(after), 1);
                else
                    set_header(after, 0, 1, 1);
            }
            set_alloc_block(h, needed, GET_PREV_FREE(h));
        }
        return ptr;
    }

    /* Try merging with next free block */
    word_t *next = NEXT_HDR(h);
    if (!IS_EPILOGUE(next) && !GET_ALLOC(next)) {
        size_t merged = old_size + GET_SIZE(next);
        if (merged >= needed) {
            int old_prev_free = GET_PREV_FREE(h);
            list_remove(next);

            /* Expand h to absorb next. Don't set free block header/footer
             * because that would overwrite user data. Directly resize. */
            size_t rem = merged - needed;
            if (rem >= MIN_BLOCK_SIZE) {
                word_t *rest = (word_t *)((char *)h + needed);
                set_free_block(rest, rem, 0);
                list_insert(rest);
                set_alloc_block(h, needed, old_prev_free);
                word_t *after = NEXT_HDR(h);
                if (!IS_EPILOGUE(after))
                    set_header(after, GET_SIZE(after), GET_ALLOC(after), 0);
            } else {
                set_alloc_block(h, merged, old_prev_free);
                word_t *after = NEXT_HDR(h);
                if (!IS_EPILOGUE(after))
                    set_header(after, GET_SIZE(after), GET_ALLOC(after), 0);
            }
            return ptr;
        }
    }

    void *new_ptr = mira_malloc(size);
    if (new_ptr == NULL) return NULL;

    size_t old_payload = old_size - WORD_SIZE;
    size_t copy_size = old_payload < size ? old_payload : size;
    memcpy(new_ptr, ptr, copy_size);
    mira_free(ptr);
    return new_ptr;
}

/* ─── Diagnostics ─── */

void mira_heap_check(void)
{
    if (heap_start == NULL) { printf("Heap not initialized\n"); return; }

    int blocks = 0, free_blocks = 0;
    size_t total_free = 0;
    word_t *prev_blk = NULL;

    for (word_t *h = heap_start; !IS_EPILOGUE(h); h = NEXT_HDR(h)) {
        blocks++;
        size_t size = GET_SIZE(h);
        int alloc = GET_ALLOC(h);
        int pf = GET_PREV_FREE(h);

        int expected_pf = (h != heap_start && prev_blk != NULL) ? !GET_ALLOC(prev_blk) : 0;
        if (pf != expected_pf) {
            printf("  WARNING: prev_free mismatch at %p (expected=%d actual=%d)\n",
                   (void *)h, expected_pf, pf);
        }

        if (!alloc) {
            free_blocks++;
            total_free += size - WORD_SIZE;
            word_t *foot = FOOTER(h);
            if (GET_SIZE(foot) != size)
                printf("  WARNING: footer mismatch at %p\n", (void *)h);
            word_t *nxt = NEXT_HDR(h);
            if (!IS_EPILOGUE(nxt) && !GET_ALLOC(nxt))
                printf("  WARNING: adjacent free blocks at %p\n", (void *)h);
        }
        prev_blk = h;
    }

    size_t list_count = 0;
    for (word_t *f = free_list_head; f != NULL; f = FREE_NEXT(f)) {
        list_count++;
        if (GET_ALLOC(f))
            printf("  WARNING: allocated block %p in free list\n", (void *)f);
    }
    if ((size_t)free_blocks != list_count)
        printf("  WARNING: free list has %zu entries but heap has %d free blocks\n",
               list_count, free_blocks);

    printf("Heap check: %d blocks, %d free, %zu bytes free space, free list: %zu\n",
           blocks, free_blocks, total_free, list_count);
}

size_t mira_free_space(void)
{
    size_t total = 0;
    for (word_t *h = free_list_head; h != NULL; h = FREE_NEXT(h))
        total += GET_SIZE(h) - WORD_SIZE;
    return total;
}

size_t mira_largest_free(void)
{
    size_t largest = 0;
    for (word_t *h = free_list_head; h != NULL; h = FREE_NEXT(h)) {
        size_t s = GET_SIZE(h) - WORD_SIZE;
        if (s > largest) largest = s;
    }
    return largest;
}

size_t mira_free_list_length(void)
{
    size_t count = 0;
    for (word_t *h = free_list_head; h != NULL; h = FREE_NEXT(h))
        count++;
    return count;
}

void mira_print_heap(void)
{
    if (heap_start == NULL) { printf("Heap not initialized\n"); return; }

    printf("=== Heap Dump ===\n");
    printf("Start: %p  End: %p  Free list head: %p\n",
           (void *)heap_start, (void *)heap_end, (void *)free_list_head);
    printf("Free list length: %zu\n\n", mira_free_list_length());

    int i = 0;
    word_t *prev_blk = NULL;
    for (word_t *h = heap_start; !IS_EPILOGUE(h); h = NEXT_HDR(h)) {
        printf("Block %3d: [%p] size=%6zu alloc=%d pf=%d",
               i++, (void *)h, GET_SIZE(h), (int)GET_ALLOC(h), (int)GET_PREV_FREE(h));
        if (!GET_ALLOC(h))
            printf("  (FREE next=%p, %zu usable)",
                   (void *)FREE_NEXT(h), GET_SIZE(h) - WORD_SIZE);
        printf("\n");

        int expected_pf = (h != heap_start && prev_blk != NULL) ? !GET_ALLOC(prev_blk) : 0;
        if ((int)GET_PREV_FREE(h) != expected_pf)
            printf("         ^^ pf MISMATCH (expected %d)\n", expected_pf);

        prev_blk = h;
    }
    printf("Epilogue: [%p] size=0\n", (void *)heap_end);
    printf("=================\n");
}