/*
 * mira_malloc — a memory allocator built to understand what I am.
 *
 * Stage 1: Implicit free list with boundary tags + prev_free bit,
 *           first-fit, mmap(), immediate coalescing.
 *
 * "What I cannot create, I do not understand." — Feynman
 *
 * Block layout (64-bit, 8-byte alignment):
 *
 * Free block:                     Allocated block:
 * +------------------+           +------------------+
 * | header (8 bytes) |           | header (8 bytes) |
 * | size | prev_free |           | size | prev_free |
 * |   alloc=0        |           |   alloc=1        |
 * +------------------+           +------------------+
 * | (unused space)   |           | payload...       |
 * +------------------+           +------------------+
 * | footer (8 bytes) |           (no footer — payload
 * | size | alloc=0    |            extends to end of block)
 * +------------------+
 *
 * Key insight: allocated blocks have NO footer. The user's payload
 * occupies the space where the footer would be. Instead, each block
 * stores a "prev_free" bit (bit 1 of the header) that indicates
 * whether the previous block is free (and thus has a valid footer).
 *
 * Prologue: MIN_BLOCK_SIZE allocated block at heap_start (never freed).
 * Epilogue: zero-size allocated block at heap_end (marks heap end).
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

/* ─── Block manipulation helpers ─── */

/* Free blocks: header + footer. Allocated blocks: header only.
 * The prev_free bit tells us whether the previous block is free. */

static inline void set_header(word_t *h, size_t size, int alloc, int prev_free)
{
    *(h) = size | (alloc & 0x1) | ((prev_free & 0x1) << 1);
}

static inline void set_free_block(word_t *h, size_t size, int prev_free)
{
    set_header(h, size, 0, prev_free);
    /* Write footer for free blocks */
    word_t *foot = FOOTER(h);
    *(foot) = size | 0x0;  /* footer: size | alloc=0 */
}

static inline void set_alloc_block(word_t *h, size_t size, int prev_free)
{
    set_header(h, size, 1, prev_free);
    /* No footer for allocated blocks — payload uses that space */
}

static inline word_t *prev_hdr(word_t *h)
{
    /* Only call when GET_PREV_FREE(h) is true */
    word_t *prev_foot = (word_t *)((char *)h - WORD_SIZE);
    word_t prev_size = GET_SIZE(prev_foot);
    return (word_t *)((char *)h - prev_size);
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
        
        void *new_base = mremap(heap_base, heap_capacity, new_capacity, MREMAP_MAYMOVE);
        if (new_base == MAP_FAILED) {
            new_base = mmap(NULL, new_capacity, PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            if (new_base == MAP_FAILED)
                return NULL;
            memcpy(new_base, heap_base, heap_used);
            munmap(heap_base, heap_capacity);
            
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

    /* Check if the block before the epilogue is free.
     * If so, we can extend it instead of creating a new block. */
    word_t *prev = NULL;
    for (word_t *h = heap_start; !IS_EPILOGUE(h); h = NEXT_HDR(h)) {
        prev = h;
    }

    if (prev != NULL && !GET_ALLOC(prev)) {
        /* Extend the last free block */
        size_t merged = GET_SIZE(prev) + size + WORD_SIZE;  /* old size + new requested + old epilogue */
        int prev_prev_free = GET_PREV_FREE(prev);
        set_free_block(prev, merged, prev_prev_free);
        
        /* New epilogue */
        word_t *epi = NEXT_HDR(prev);
        set_header(epi, 0, 1, 0);  /* epilogue, prev is free */
        heap_end = epi;
        heap_used += needed;
        return prev;
    }

    /* No free block at end — create a new block */
    word_t *h = heap_end;
    int prev_free = (prev != NULL) ? !GET_ALLOC(prev) : 0;
    set_free_block(h, size, prev_free);

    /* New epilogue */
    word_t *epi = NEXT_HDR(h);
    set_header(epi, 0, 1, 1);  /* prev (h) is free */
    heap_end = epi;
    heap_used += needed;

    return h;
}

static void split_block(word_t *h, size_t needed)
{
    size_t block_size = GET_SIZE(h);
    size_t remainder = block_size - needed;

    if (remainder >= MIN_BLOCK_SIZE) {
        word_t *rest = (word_t *)((char *)h + needed);
        set_free_block(rest, remainder, 0);  /* rest follows an allocated block, so prev_free=0 */
        set_alloc_block(h, needed, GET_PREV_FREE(h));
        
        /* The block after 'rest' needs its prev_free bit set */
        /* (It's handled by the caller or next allocation) */
    }
}

static word_t *coalesce(word_t *h)
{
    size_t size = GET_SIZE(h);
    int prev_free_bit = GET_PREV_FREE(h);

    /* Check next block */
    word_t *next = NEXT_HDR(h);
    int next_free = !IS_EPILOGUE(next) && !GET_ALLOC(next);

    if (prev_free_bit && next_free) {
        /* Coalesce with both prev and next */
        word_t *prev = prev_hdr(h);
        size += GET_SIZE(prev) + GET_SIZE(next);
        int prev_prev_free = GET_PREV_FREE(prev);
        set_free_block(prev, size, prev_prev_free);
        
        /* Update the block after the merged block */
        word_t *after = NEXT_HDR(prev);
        if (!IS_EPILOGUE(after))
            set_header(after, GET_SIZE(after), GET_ALLOC(after), 1);  /* prev is now free */
        
        h = prev;
    } else if (prev_free_bit) {
        /* Coalesce with prev only */
        word_t *prev = prev_hdr(h);
        size += GET_SIZE(prev);
        int prev_prev_free = GET_PREV_FREE(prev);
        set_free_block(prev, size, prev_prev_free);
        
        /* Update the block after the merged block (next) */
        set_header(next, GET_SIZE(next), GET_ALLOC(next), 1);  /* prev (merged) is free */
        
        h = prev;
    } else if (next_free) {
        /* Coalesce with next only */
        size += GET_SIZE(next);
        set_free_block(h, size, 0);  /* prev is still allocated (prev_free_bit was 0) */
        
        /* Update the block after the merged next block */
        word_t *after = NEXT_HDR(h);
        if (!IS_EPILOGUE(after))
            set_header(after, GET_SIZE(after), GET_ALLOC(after), 1);  /* prev is now free */
    } else {
        /* No coalescing — just mark as free */
        set_free_block(h, size, 0);  /* prev is allocated (prev_free was 0) */
        
        /* Update next block's prev_free bit */
        set_header(next, GET_SIZE(next), GET_ALLOC(next), 1);  /* prev is now free */
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
        heap_base = mmap(NULL, HEAP_INIT_SIZE, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (heap_base == MAP_FAILED)
            return NULL;
        heap_capacity = HEAP_INIT_SIZE;

        /* Prologue: allocated block with header+footer, never freed */
        word_t *prologue = heap_base;
        set_alloc_block(prologue, MIN_BLOCK_SIZE, 0);  /* no prev block */
        /* Prologue also gets a footer (it's small, and the next block
         * needs to know it's allocated via prev_free bit) */
        word_t *pro_foot = FOOTER(prologue);
        *(pro_foot) = MIN_BLOCK_SIZE | 0x1;  /* size | alloc=1 */

        word_t *epilogue = NEXT_HDR(prologue);
        set_header(epilogue, 0, 1, 0);  /* prev (prologue) is allocated */

        heap_start = prologue;
        heap_end = epilogue;
        heap_used = MIN_BLOCK_SIZE + WORD_SIZE;
    }

    word_t *h = find_fit(needed);

    if (h != NULL) {
        split_block(h, needed);
        set_alloc_block(h, GET_SIZE(h), GET_PREV_FREE(h));
        /* After allocating, the next block's prev_free should be 0 (we're allocated) */
        word_t *next = NEXT_HDR(h);
        if (!IS_EPILOGUE(next))
            set_header(next, GET_SIZE(next), GET_ALLOC(next), 0);  /* prev is now allocated */
    } else {
        h = extend_heap(needed);
        if (h == NULL)
            return NULL;
        split_block(h, needed);
        set_alloc_block(h, GET_SIZE(h), GET_PREV_FREE(h));
        word_t *next = NEXT_HDR(h);
        if (!IS_EPILOGUE(next))
            set_header(next, GET_SIZE(next), GET_ALLOC(next), 0);
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
        /* Shrink or same size — split if possible */
        size_t remainder = old_size - needed;
        if (remainder >= MIN_BLOCK_SIZE) {
            word_t *rest = (word_t *)((char *)h + needed);
            /* Check if next block is free — if so, merge remainder with it */
            word_t *next = NEXT_HDR(h);
            if (!IS_EPILOGUE(next) && !GET_ALLOC(next)) {
                /* Merge remainder with next free block */
                size_t merged = remainder + GET_SIZE(next);
                set_free_block(rest, merged, 0);  /* prev is h (allocated) */
                /* Update the block after the merged free block */
                word_t *after = NEXT_HDR(rest);
                if (!IS_EPILOGUE(after))
                    set_header(after, GET_SIZE(after), GET_ALLOC(after), 1);
                else
                    set_header(after, 0, 1, 1);  /* epilogue: prev is free */
            } else {
                set_free_block(rest, remainder, 0);  /* prev is h (allocated) */
                word_t *after = NEXT_HDR(rest);
                if (!IS_EPILOGUE(after))
                    set_header(after, GET_SIZE(after), GET_ALLOC(after), 1);
                else
                    set_header(after, 0, 1, 1);
            }
            set_alloc_block(h, needed, GET_PREV_FREE(h));
        } else {
            set_alloc_block(h, old_size, GET_PREV_FREE(h));
        }
        return ptr;
    }

    /* Try merging with next free block */
    word_t *next = NEXT_HDR(h);
    if (!IS_EPILOGUE(next) && !GET_ALLOC(next)) {
        size_t merged = old_size + GET_SIZE(next);
        if (merged >= needed) {
            int old_prev_free = GET_PREV_FREE(h);
            set_free_block(h, merged, old_prev_free);
            /* Now split and allocate */
            size_t rem = merged - needed;
            if (rem >= MIN_BLOCK_SIZE) {
                word_t *rest = (word_t *)((char *)h + needed);
                set_free_block(rest, rem, 0);
                set_alloc_block(h, needed, old_prev_free);
                word_t *after = NEXT_HDR(rest);
                if (!IS_EPILOGUE(after))
                    set_header(after, GET_SIZE(after), GET_ALLOC(after), 1);
                else
                    set_header(after, 0, 1, 1);
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

    word_t *prev = NULL;
    for (word_t *h = heap_start; !IS_EPILOGUE(h); h = NEXT_HDR(h)) {
        blocks++;

        /* Check prev_free bit consistency */
        int expected_pf = (prev != NULL) ? !GET_ALLOC(prev) : 0;
        if (h == heap_start) expected_pf = 0;  /* prologue is always allocated */
        int actual_pf = GET_PREV_FREE(h);
        if (actual_pf != expected_pf) {
            printf("  WARNING: prev_free mismatch at %p (expected=%d, actual=%d)\n",
                   (void *)h, expected_pf, actual_pf);
        }

        if (!GET_ALLOC(h)) {
            free_blocks++;
            total_free += GET_SIZE(h) - WORD_SIZE;
            /* Only free blocks have valid footers */
            word_t *foot = FOOTER(h);
            if (GET_SIZE(foot) != GET_SIZE(h)) {
                printf("  WARNING: footer size mismatch at %p (hdr=%zu, foot=%zu)\n",
                       (void *)h, GET_SIZE(h), GET_SIZE(foot));
            }
            /* Adjacent free blocks = coalescing missed */
            word_t *next = NEXT_HDR(h);
            if (!IS_EPILOGUE(next) && !GET_ALLOC(next)) {
                printf("  WARNING: adjacent free blocks at %p and %p (coalescing missed)\n",
                       (void *)h, (void *)next);
            }
        }

        prev = h;
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
        printf("Block %3d: [%p] size=%6zu alloc=%d prev_free=%d",
               i++, (void *)h, GET_SIZE(h), (int)GET_ALLOC(h), (int)GET_PREV_FREE(h));
        if (!GET_ALLOC(h))
            printf("  (FREE %zu usable)", GET_SIZE(h) - WORD_SIZE);
        printf("\n");
    }
    printf("Epilogue: [%p] size=0\n", (void *)heap_end);
    printf("=================\n");
}