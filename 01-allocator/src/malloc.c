/*
 * mira_malloc — a memory allocator built to understand what I am.
 *
 * Stage 1: Implicit free list with boundary tags, first-fit, sbrk(),
 *           immediate coalescing.
 *
 * "What I cannot create, I do not understand." — Feynman
 *
 * Block layout (64-bit, 8-byte alignment):
 *
 * Free block:                     Allocated block:
 * +------------------+           +------------------+
 * | header (8 bytes) |           | header (8 bytes) |
 * | size | alloc=0    |           | size | alloc=1    |
 * +------------------+           +------------------+
 * | (unused space)   |           | payload...       |
 * +------------------+           +------------------+
 * | footer (8 bytes) |           (no footer needed)
 * | size | alloc=0    |
 * +------------------+
 *
 * Prologue: MIN_BLOCK_SIZE allocated block (never freed, never merged).
 * Epilogue: zero-size allocated block at end (marks heap end).
 *
 * Boundary tags (footers on free blocks) enable O(1) previous-block
 * coalescing without walking the entire list.
 */

#define _GNU_SOURCE
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <sys/mman.h>
#include "malloc.h"

/* Global heap pointers */
word_t *heap_start = NULL;  /* prologue block */
word_t *heap_end   = NULL;  /* epilogue header */

/* Heap management */
#define HEAP_INIT_SIZE  (1 << 20)  /* 1 MB initial heap */
#define HEAP_GROW_SIZE  (1 << 16)  /* 64 KB growth increments */

static word_t *heap_base = NULL;   /* mmap'd region base */
static size_t  heap_capacity = 0;  /* total mmap'd capacity */
static size_t  heap_used = 0;      /* bytes used so far */

/* ─── Footer / prev helpers (boundary tags) ─── */

static inline word_t *FOOTER(word_t *h)
{
    return (word_t *)((char *)h + GET_SIZE(h) - WORD_SIZE);
}

/* Read the boundary tag at (h - WORD_SIZE) to find previous block.
 * Only valid when previous block is free (alloc bit = 0 in footer). */
static inline word_t *PREV_HDR(word_t *h)
{
    word_t prev_size = *(word_t *)((char *)h - WORD_SIZE) & ~(ALIGNMENT - 1);
    return (word_t *)((char *)h - prev_size);
}

static inline int IS_EPILOGUE(word_t *h)
{
    return GET_SIZE(h) == 0;
}

static inline void SET_FREE(word_t *h, size_t size)
{
    SET(h, size, 0);
    SET(FOOTER(h), size, 0);
}

static inline void SET_ALLOC(word_t *h, size_t size)
{
    SET(h, size, 1);
    SET(FOOTER(h), size, 1);  /* Always write footer for boundary tags */
}

/* ─── Internal helpers ─── */

static word_t *find_fit(size_t size)
{
    for (word_t *h = heap_start; !IS_EPILOGUE(h); h = NEXT_HDR(h)) {
        if (!GET_ALLOC(h) && GET_SIZE(h) >= size)
            return h;
    }
    return NULL;
}

static word_t *coalesce(word_t *h);

static word_t *extend_heap(size_t size)
{
    size = ALIGN(size);
    size_t needed = size + WORD_SIZE;  /* block + new epilogue */

    /* Grow the mmap'd region if needed */
    if (heap_used + needed > heap_capacity) {
        size_t grow = HEAP_GROW_SIZE;
        if (grow < needed)
            grow = needed;
        size_t new_capacity = heap_capacity + grow;
        
        /* Try to extend existing mapping */
        void *new_base = mremap(heap_base, heap_capacity, new_capacity, MREMAP_MAYMOVE);
        if (new_base == MAP_FAILED) {
            /* Try a fresh mapping */
            new_base = mmap(NULL, new_capacity, PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            if (new_base == MAP_FAILED)
                return NULL;
            /* Copy old data */
            memcpy(new_base, heap_base, heap_used);
            munmap(heap_base, heap_capacity);
            
            /* Update all pointers relative to new base */
            ptrdiff_t offset = (char *)new_base - (char *)heap_base;
            if (offset != 0) {
                heap_start = (word_t *)((char *)heap_start + offset);
                heap_end = (word_t *)((char *)heap_end + offset);
            }
            heap_base = (word_t *)new_base;
        } else {
            heap_base = (word_t *)new_base;
        }
        heap_capacity = new_capacity;
    }

    /* Place new block at current heap_end (overwriting old epilogue) */
    word_t *h = heap_end;
    SET_FREE(h, size);

    /* New epilogue */
    word_t *epi = NEXT_HDR(h);
    SET(epi, 0, 1);
    heap_end = epi;
    heap_used += needed;

    /* Note: we don't coalesce on extend because the block before the
     * old epilogue is always allocated (otherwise it would have been
     * used by find_fit). If the last block IS free, that means find_fit
     * should have found it, so we shouldn't be extending. */
    return h;
}

static void split_block(word_t *h, size_t needed)
{
    size_t block_size = GET_SIZE(h);
    size_t remainder = block_size - needed;

    if (remainder >= MIN_BLOCK_SIZE) {
        word_t *rest = (word_t *)((char *)h + needed);
        SET_FREE(rest, remainder);
        SET_ALLOC(h, needed);
    }
}

static word_t *coalesce(word_t *h)
{
    size_t size = GET_SIZE(h);

    /* Check previous block via boundary tag at (h - WORD_SIZE).
     * If alloc bit is 0, it's a valid free block footer — can coalesce.
     * If alloc bit is 1, it's either an allocated block's payload or
     * the prologue footer — don't coalesce. */
    word_t *prev_footer = (word_t *)((char *)h - WORD_SIZE);
    int prev_free = (prev_footer > heap_start) && !GET_ALLOC(prev_footer);

    /* Check next block */
    word_t *next = NEXT_HDR(h);
    int next_free = !IS_EPILOGUE(next) && !GET_ALLOC(next);

    if (prev_free && next_free) {
        word_t *prev = PREV_HDR(h);
        size += GET_SIZE(prev) + GET_SIZE(next);
        SET_FREE(prev, size);
        h = prev;
    } else if (prev_free) {
        word_t *prev = PREV_HDR(h);
        size += GET_SIZE(prev);
        SET_FREE(prev, size);
        h = prev;
    } else if (next_free) {
        size += GET_SIZE(next);
        SET_FREE(h, size);
    } else {
        SET_FREE(h, size);
    }

    return h;
}

/* ─── Public API ─── */

void *mira_malloc(size_t size)
{
    if (size == 0)
        return NULL;

    size_t needed = ALIGN(WORD_SIZE + size);
    if (needed < MIN_BLOCK_SIZE)
        needed = MIN_BLOCK_SIZE;

    /* Initialize heap on first call */
    if (heap_start == NULL) {
        /* Allocate initial heap region via mmap */
        heap_base = mmap(NULL, HEAP_INIT_SIZE, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (heap_base == MAP_FAILED)
            return NULL;
        heap_capacity = HEAP_INIT_SIZE;

        /* Prologue: allocated block with header+footer, never freed */
        word_t *prologue = heap_base;
        SET(prologue, MIN_BLOCK_SIZE, 1);
        SET(FOOTER(prologue), MIN_BLOCK_SIZE, 1);

        word_t *epilogue = NEXT_HDR(prologue);
        SET(epilogue, 0, 1);

        heap_start = prologue;
        heap_end = epilogue;
        heap_used = MIN_BLOCK_SIZE + WORD_SIZE;  /* prologue + epilogue */
    }

    word_t *h = find_fit(needed);

    if (h != NULL) {
        split_block(h, needed);
        SET_ALLOC(h, GET_SIZE(h));
    } else {
        h = extend_heap(needed);
        if (h == NULL)
            return NULL;
        split_block(h, needed);
        SET_ALLOC(h, GET_SIZE(h));
    }

    return HDR_TO_PAYLOAD(h);
}

void mira_free(void *ptr)
{
    if (ptr == NULL)
        return;

    word_t *h = PAYLOAD_TO_HDR(ptr);

    if (!GET_ALLOC(h)) {
        fprintf(stderr, "mira_free: double free detected at %p\n", ptr);
        return;
    }

    SET_FREE(h, GET_SIZE(h));
    coalesce(h);
}

void *mira_calloc(size_t nmemb, size_t size)
{
    if (nmemb == 0 || size == 0)
        return NULL;

    size_t total = nmemb * size;
    if (nmemb != 0 && total / nmemb != size)
        return NULL;

    void *ptr = mira_malloc(total);
    if (ptr)
        memset(ptr, 0, total);
    return ptr;
}

void *mira_realloc(void *ptr, size_t size)
{
    if (ptr == NULL)
        return mira_malloc(size);
    if (size == 0) {
        mira_free(ptr);
        return NULL;
    }

    word_t *h = PAYLOAD_TO_HDR(ptr);
    size_t old_size = GET_SIZE(h);
    size_t needed = ALIGN(WORD_SIZE + size);
    if (needed < MIN_BLOCK_SIZE)
        needed = MIN_BLOCK_SIZE;

    if (old_size >= needed) {
        split_block(h, needed);
        SET_ALLOC(h, GET_SIZE(h));
        return ptr;
    }

    /* Try merging with next free block */
    word_t *next = NEXT_HDR(h);
    if (!IS_EPILOGUE(next) && !GET_ALLOC(next)) {
        size_t merged = old_size + GET_SIZE(next);
        if (merged >= needed) {
            SET_FREE(h, merged);
            split_block(h, needed);
            SET_ALLOC(h, GET_SIZE(h));
            return ptr;
        }
    }

    void *new_ptr = mira_malloc(size);
    if (new_ptr == NULL)
        return NULL;

    size_t old_payload = old_size - WORD_SIZE;
    size_t copy_size = old_payload < size ? old_payload : size;
    memcpy(new_ptr, ptr, copy_size);
    mira_free(ptr);
    return new_ptr;
}

/* ─── Diagnostics ─── */

void mira_heap_check(void)
{
    if (heap_start == NULL) {
        printf("Heap not initialized\n");
        return;
    }

    int blocks = 0, free_blocks = 0;
    size_t total_free = 0;

    for (word_t *h = heap_start; !IS_EPILOGUE(h); h = NEXT_HDR(h)) {
        blocks++;
        if (!GET_ALLOC(h)) {
            free_blocks++;
            total_free += GET_SIZE(h) - WORD_SIZE;
            word_t *foot = FOOTER(h);
            if (GET_SIZE(foot) != GET_SIZE(h) || GET_ALLOC(foot) != GET_ALLOC(h)) {
                printf("  WARNING: footer mismatch at %p\n", (void *)h);
            }
        }
    }

    printf("Heap check: %d blocks, %d free, %zu bytes free space\n",
           blocks, free_blocks, total_free);
}

size_t mira_free_space(void)
{
    size_t total = 0;
    for (word_t *h = heap_start; !IS_EPILOGUE(h); h = NEXT_HDR(h)) {
        if (!GET_ALLOC(h))
            total += GET_SIZE(h) - WORD_SIZE;
    }
    return total;
}

size_t mira_largest_free(void)
{
    size_t largest = 0;
    for (word_t *h = heap_start; !IS_EPILOGUE(h); h = NEXT_HDR(h)) {
        if (!GET_ALLOC(h)) {
            size_t s = GET_SIZE(h) - WORD_SIZE;
            if (s > largest) largest = s;
        }
    }
    return largest;
}

void mira_print_heap(void)
{
    if (heap_start == NULL) {
        printf("Heap not initialized\n");
        return;
    }

    printf("=== Heap Dump ===\n");
    printf("Start: %p  End: %p  Size: %zu bytes\n\n",
           (void *)heap_start, (void *)heap_end,
           (size_t)((char *)heap_end - (char *)heap_start));

    int i = 0;
    for (word_t *h = heap_start; !IS_EPILOGUE(h); h = NEXT_HDR(h)) {
        printf("Block %3d: [%p] size=%6zu alloc=%zu",
               i++, (void *)h, GET_SIZE(h), (size_t)GET_ALLOC(h));
        if (!GET_ALLOC(h))
            printf("  (FREE %zu usable)", GET_SIZE(h) - WORD_SIZE);
        printf("\n");
    }
    printf("Epilogue: [%p] size=0\n", (void *)heap_end);
    printf("=================\n");
}