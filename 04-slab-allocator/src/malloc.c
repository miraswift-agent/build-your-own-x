/*
 * mira_malloc — Stage 4: Slab allocator for small objects
 *
 * Small objects (≤ 256 bytes): slab pools with bitmap allocation.
 * No per-object header. O(1) malloc and free via bitmap scan.
 *
 * Large objects (> 256 bytes): segregated free lists (Stage 3).
 * Boundary tags, prev_free bit, singly-linked per-bucket lists.
 *
 * "What I cannot create, I do not understand." — Feynman
 */

#define _GNU_SOURCE
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <sys/mman.h>
#include "malloc.h"

/* ─── Slab size classes ─── */
/* 12 classes: 16, 32, 48, 64, 80, 96, 112, 128, 160, 192, 224, 256 */

static const size_t slab_sizes[NUM_SLAB_CLASSES] = {
    16, 32, 48, 64, 80, 96, 112, 128, 160, 192, 224, 256
};

/* ─── Slab allocator state ─── */

slab_header_t *slab_lists[NUM_SLAB_CLASSES] = {0};

/* ─── Free list state (Stage 3) ─── */

static word_t *free_buckets[NUM_SIZE_CLASSES];

word_t *heap_start = NULL;
word_t *heap_end   = NULL;

static word_t *heap_base = NULL;
static size_t  heap_capacity = 0;
static size_t  heap_used = 0;

#define HEAP_INIT_SIZE  (1 << 20)
#define HEAP_GROW_SIZE  (1 << 16)

/* ─── Inline helpers ─── */

static inline void set_header(word_t *h, size_t size, int alloc, int prev_free)
{
    *(h) = size | (alloc & 0x1) | ((prev_free & 0x1) << 1);
}

static inline void set_free_block(word_t *h, size_t size, int prev_free)
{
    set_header(h, size, 0, prev_free);
    word_t *foot = FOOTER(h);
    *foot = size;
}

static inline void set_alloc_block(word_t *h, size_t size, int prev_free)
{
    set_header(h, size, 1, prev_free);
}

/* ─── Slab helpers ─── */

/* ptr_slab_class removed — use find_slab instead */

static slab_header_t *find_slab(void *ptr)
{
    slab_header_t *candidate = (slab_header_t *)((size_t)ptr & ~(SLAB_ALIGN - 1));
    if (candidate->magic == SLAB_MAGIC)
        return candidate;
    return NULL;
}

static size_t slab_bitmap_bytes(size_t n_slots)
{
    return ((n_slots + 7) / 8 + ALIGNMENT - 1) & ~(ALIGNMENT - 1);
}

static slab_header_t *create_slab(int cls)
{
    size_t slot_size = slab_sizes[cls];
    size_t hdr_size = sizeof(slab_header_t) + slab_bitmap_bytes(256);
    size_t slab_size = SLAB_ALIGN;

    /* Ensure slab is large enough for header + at least SLAB_MIN_SLOTS slots */
    size_t min_size = hdr_size + slot_size * SLAB_MIN_SLOTS;
    if (min_size > slab_size) {
        slab_size = (min_size + SLAB_ALIGN - 1) & ~(SLAB_ALIGN - 1);
    }

    void *mem = mmap(NULL, slab_size, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mem == MAP_FAILED) return NULL;

    slab_header_t *slab = (slab_header_t *)mem;
    slab->magic = SLAB_MAGIC;
    slab->_pad = 0;
    slab->slot_size = slot_size;
    slab->n_slots = (slab_size - ((char *)SLAB_DATA(slab) - (char *)slab)) / slot_size;
    slab->n_used = 0;
    slab->next = NULL;

    /* Clear bitmap */
    memset(SLAB_BITMAP(slab), 0, slab_bitmap_bytes(slab->n_slots));

    return slab;
}

static void *slab_alloc(int cls)
{
    slab_header_t *slab = slab_lists[cls];

    /* Try existing slabs first */
    while (slab != NULL) {
        if (slab->n_used < slab->n_slots) {
            /* Find free slot via bitmap */
            unsigned char *bm = SLAB_BITMAP(slab);
            for (size_t i = 0; i < slab->n_slots; i++) {
                if (!(bm[i / 8] & (1 << (i % 8)))) {
                    /* Found free slot */
                    bm[i / 8] |= (1 << (i % 8));
                    slab->n_used++;
                    char *data = (char *)SLAB_DATA(slab);
                    return data + i * slab->slot_size;
                }
            }
        }
        slab = slab->next;
    }

    /* No free slot found, create new slab */
    slab = create_slab(cls);
    if (slab == NULL) return NULL;

    /* Prepend to list */
    slab->next = slab_lists[cls];
    slab_lists[cls] = slab;

    /* Allocate first slot */
    unsigned char *bm = SLAB_BITMAP(slab);
    bm[0] |= 1;
    slab->n_used = 1;
    return SLAB_DATA(slab);
}

static void slab_free(void *ptr, slab_header_t *slab)
{
    char *data = (char *)SLAB_DATA(slab);
    size_t index = ((char *)ptr - data) / slab->slot_size;

    unsigned char *bm = SLAB_BITMAP(slab);
    bm[index / 8] &= ~(1 << (index % 8));
    slab->n_used--;

    /* Reap: if slab is completely empty and there's another empty slab in same class,
       unmap this one. Keep one empty slab as reserve. */
    if (slab->n_used == 0) {
        int cls = (int)((slab->slot_size / 16) - 1);
        int empty_count = 0;
        for (slab_header_t *s = slab_lists[cls]; s != NULL; s = s->next) {
            if (s->n_used == 0) empty_count++;
        }
        if (empty_count > 1) {
            /* Remove from list */
            slab_header_t **prev = &slab_lists[cls];
            while (*prev != NULL && *prev != slab)
                prev = &(*prev)->next;
            if (*prev == slab) {
                *prev = slab->next;
                size_t slab_total = sizeof(slab_header_t) +
                    slab_bitmap_bytes(slab->n_slots) +
                    slab->n_slots * slab->slot_size;
                slab_total = (slab_total + SLAB_ALIGN - 1) & ~(SLAB_ALIGN - 1);
                munmap(slab, slab_total);
            }
        }
    }
}

/* ─── Free list operations (Stage 3) ─── */

static inline void bucket_insert(word_t *h)
{
    int cls = size_class(GET_SIZE(h));
    FREE_NEXT(h) = free_buckets[cls];
    free_buckets[cls] = h;
}

static void bucket_remove(word_t *h)
{
    int cls = size_class(GET_SIZE(h));
    if (free_buckets[cls] == h) {
        free_buckets[cls] = FREE_NEXT(h);
    } else {
        for (word_t *prev = free_buckets[cls]; prev != NULL; prev = FREE_NEXT(prev)) {
            if (FREE_NEXT(prev) == h) {
                FREE_NEXT(prev) = FREE_NEXT(h);
                return;
            }
        }
    }
    FREE_NEXT(h) = NULL;
}

static word_t *bucket_find(size_t size)
{
    int cls = size_class(size);
    for (word_t *h = free_buckets[cls]; h != NULL; h = FREE_NEXT(h)) {
        if (GET_SIZE(h) >= size)
            return h;
    }
    for (int i = cls + 1; i < NUM_SIZE_CLASSES; i++) {
        for (word_t *h = free_buckets[i]; h != NULL; h = FREE_NEXT(h)) {
            if (GET_SIZE(h) >= size)
                return h;
        }
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
        bucket_insert(rest);

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
        bucket_remove(prev);
        bucket_remove(next);

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
        bucket_remove(prev);

        size += GET_SIZE(prev);
        int prev_prev_free = GET_PREV_FREE(prev);
        set_free_block(prev, size, prev_prev_free);

        set_header(next, GET_SIZE(next), GET_ALLOC(next), 1);
        h = prev;
    } else if (next_free) {
        bucket_remove(next);

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

    bucket_insert(h);
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
                heap_start = (word_t *)((char *)heap_start + offset);
                heap_end = (word_t *)((char *)heap_end + offset);
                for (int i = 0; i < NUM_SIZE_CLASSES; i++) {
                    if (free_buckets[i] != NULL)
                        free_buckets[i] = (word_t *)((char *)free_buckets[i] + offset);
                }
                for (int i = 0; i < NUM_SIZE_CLASSES; i++) {
                    for (word_t *f = free_buckets[i]; f != NULL; f = FREE_NEXT(f)) {
                        if (FREE_NEXT(f) != NULL)
                            FREE_NEXT(f) = (word_t *)((char *)FREE_NEXT(f) + offset);
                    }
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
        if (GET_SIZE(p) < MIN_BLOCK_SIZE || (GET_SIZE(p) & (ALIGNMENT - 1)) != 0) {
            fprintf(stderr, "mira_malloc: heap corruption at %p (size=%zu)\n", (void *)p, GET_SIZE(p));
            return NULL;
        }
        prev = p;
    }

    if (prev != NULL && !GET_ALLOC(prev)) {
        size_t merged = GET_SIZE(prev) + size + WORD_SIZE;
        int prev_prev_free = GET_PREV_FREE(prev);
        bucket_remove(prev);
        set_free_block(prev, merged, prev_prev_free);

        word_t *epi = NEXT_HDR(prev);
        set_header(epi, 0, 1, 1);
        heap_end = epi;
        heap_used += needed;
        bucket_insert(prev);
        return prev;
    }

    word_t *h = heap_end;
    int prev_free_val = (prev != NULL) ? !GET_ALLOC(prev) : 0;
    set_free_block(h, size, prev_free_val);

    word_t *epi = NEXT_HDR(h);
    set_header(epi, 0, 1, 1);
    heap_end = epi;
    heap_used += needed;
    bucket_insert(h);
    return h;
}

/* ─── Public API ─── */

void *mira_malloc(size_t size)
{
    if (size == 0) return NULL;

    /* Small allocation: try slab allocator */
    if (size <= SMALL_THRESHOLD) {
        int cls = (int)((size + 15) / 16) - 1;
        if (cls < 0) cls = 0;
        if (cls >= NUM_SLAB_CLASSES) cls = NUM_SLAB_CLASSES - 1;

        /* Ensure size fits in slot */
        if (size > slab_sizes[cls]) {
            /* Round up to next slab class */
            for (int i = cls; i < NUM_SLAB_CLASSES; i++) {
                if (slab_sizes[i] >= size) { cls = i; break; }
            }
        }

        void *ptr = slab_alloc(cls);
        if (ptr) return ptr;
        /* Fall through to free list allocator if slab fails */
    }

    /* Large allocation: segregated free lists */
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

    word_t *h = bucket_find(needed);

    if (h != NULL) {
        bucket_remove(h);
        split_block(h, needed);
        set_alloc_block(h, GET_SIZE(h), GET_PREV_FREE(h));
        word_t *next = NEXT_HDR(h);
        if (!IS_EPILOGUE(next))
            set_header(next, GET_SIZE(next), GET_ALLOC(next), 0);
    } else {
        h = extend_heap(needed);
        if (h == NULL) return NULL;
        bucket_remove(h);
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
    if (ptr == NULL) return;

    /* Check if this is a slab allocation */
    slab_header_t *slab = find_slab(ptr);
    if (slab != NULL) {
        /* Verify pointer is within slab data area */
        char *data = (char *)SLAB_DATA(slab);
        char *data_end = data + slab->n_slots * slab->slot_size;
        if ((char *)ptr >= data && (char *)ptr < data_end) {
            size_t index = ((char *)ptr - data) / slab->slot_size;
            unsigned char *bm = SLAB_BITMAP(slab);
            if (index < slab->n_slots) {
                if (!(bm[index / 8] & (1 << (index % 8)))) {
                    fprintf(stderr, "mira_free: double free detected at %p (slab slot %zu)\n", ptr, index);
                    return;
                }
                slab_free(ptr, slab);
                return;
            }
        }
        /* Pointer is in slab page but not a valid slot - fall through to free list check */
    }

    /* Free list allocation */
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

    /* Check if slab allocation */
    slab_header_t *slab = find_slab(ptr);
    if (slab != NULL) {
        /* For slab allocations, if new size fits in same slot, return ptr */
        if (size <= slab->slot_size) return ptr;
        /* Otherwise, allocate new, copy, free old */
        void *new_ptr = mira_malloc(size);
        if (new_ptr == NULL) return NULL;
        size_t old_usable = slab->slot_size;
        size_t copy = old_usable < size ? old_usable : size;
        memcpy(new_ptr, ptr, copy);
        mira_free(ptr);
        return new_ptr;
    }

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
                bucket_remove(next);
                size_t merged = remainder + GET_SIZE(next);
                set_free_block(rest, merged, 0);
                bucket_insert(rest);
                word_t *after = NEXT_HDR(rest);
                if (!IS_EPILOGUE(after))
                    set_header(after, GET_SIZE(after), GET_ALLOC(after), 1);
                else
                    set_header(after, 0, 1, 1);
            } else {
                set_free_block(rest, remainder, 0);
                bucket_insert(rest);
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
            bucket_remove(next);

            size_t rem = merged - needed;
            if (rem >= MIN_BLOCK_SIZE) {
                word_t *rest = (word_t *)((char *)h + needed);
                set_free_block(rest, rem, 0);
                bucket_insert(rest);
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
        if (pf != expected_pf)
            printf("  WARNING: prev_free mismatch at %p\n", (void *)h);

        if (!alloc) {
            free_blocks++;
            total_free += size - WORD_SIZE;
            word_t *foot = FOOTER(h);
            if (GET_SIZE(foot) != size)
                printf("  WARNING: footer mismatch at %p\n", (void *)h);
        }

        if (size < MIN_BLOCK_SIZE || (size & (ALIGNMENT - 1)) != 0)
            printf("  WARNING: invalid size %zu at %p\n", size, (void *)h);

        prev_blk = h;
    }

    /* Check free list consistency */
    int bucket_total = (int)mira_free_list_length();
    if (bucket_total != free_blocks)
        printf("  WARNING: buckets=%d heap_free=%d\n", bucket_total, free_blocks);

    /* Check slab consistency */
    for (int i = 0; i < NUM_SLAB_CLASSES; i++) {
        for (slab_header_t *s = slab_lists[i]; s != NULL; s = s->next) {
            if (s->magic != SLAB_MAGIC)
                printf("  WARNING: slab corruption at %p\n", (void *)s);
            unsigned char *bm = SLAB_BITMAP(s);
            int used = 0;
            for (size_t j = 0; j < s->n_slots; j++)
                if (bm[j / 8] & (1 << (j % 8))) used++;
            if ((size_t)used != s->n_used)
                printf("  WARNING: slab bitmap mismatch at %p (count=%zu bitmap=%d)\n",
                       (void *)s, s->n_used, used);
        }
    }

    printf("Heap check: %d blocks, %d free, %zu bytes free\n",
           blocks, free_blocks, total_free);
}

size_t mira_free_space(void)
{
    size_t total = 0;
    for (int i = 0; i < NUM_SIZE_CLASSES; i++)
        for (word_t *h = free_buckets[i]; h != NULL; h = FREE_NEXT(h))
            total += GET_SIZE(h) - WORD_SIZE;

    /* Add slab free slots */
    for (int i = 0; i < NUM_SLAB_CLASSES; i++)
        for (slab_header_t *s = slab_lists[i]; s != NULL; s = s->next)
            total += (s->n_slots - s->n_used) * s->slot_size;

    return total;
}

size_t mira_largest_free(void)
{
    size_t largest = 0;
    for (int i = 0; i < NUM_SIZE_CLASSES; i++)
        for (word_t *h = free_buckets[i]; h != NULL; h = FREE_NEXT(h)) {
            size_t s = GET_SIZE(h) - WORD_SIZE;
            if (s > largest) largest = s;
        }

    /* Slabs offer slot_size bytes max */
    for (int i = 0; i < NUM_SLAB_CLASSES; i++)
        for (slab_header_t *s = slab_lists[i]; s != NULL; s = s->next)
            if ((size_t)s->slot_size > largest && s->n_used < s->n_slots)
                largest = s->slot_size;

    return largest;
}

size_t mira_free_list_length(void)
{
    size_t count = 0;
    for (int i = 0; i < NUM_SIZE_CLASSES; i++)
        for (word_t *h = free_buckets[i]; h != NULL; h = FREE_NEXT(h))
            count++;
    return count;
}

void mira_print_heap(void)
{
    if (heap_start == NULL) { printf("Heap not initialized\n"); return; }

    printf("=== Free-List Heap Dump ===\n");
    printf("Start: %p  End: %p\n", (void *)heap_start, (void *)heap_end);

    int i = 0;
    for (word_t *h = heap_start; !IS_EPILOGUE(h); h = NEXT_HDR(h)) {
        printf("Block %3d: [%p] size=%6zu alloc=%d pf=%d",
               i++, (void *)h, GET_SIZE(h), (int)GET_ALLOC(h), (int)GET_PREV_FREE(h));
        if (!GET_ALLOC(h))
            printf("  (FREE bucket=%d)", size_class(GET_SIZE(h)));
        printf("\n");
    }
    printf("Epilogue: [%p]\n=================\n", (void *)heap_end);
}

void mira_print_buckets(void)
{
    printf("=== Free-List Buckets ===\n");
    for (int i = 0; i < NUM_SIZE_CLASSES; i++) {
        int count = 0;
        for (word_t *h = free_buckets[i]; h != NULL; h = FREE_NEXT(h))
            count++;
        if (count > 0)
            printf("  Bucket %2d (size ≤%4zu): %d blocks\n", i, class_size(i), count);
    }
    printf("=========================\n");
}

void mira_print_slabs(void)
{
    printf("=== Slab Dump ===\n");
    for (int i = 0; i < NUM_SLAB_CLASSES; i++) {
        int slab_count = 0, total_slots = 0, used_slots = 0;
        for (slab_header_t *s = slab_lists[i]; s != NULL; s = s->next) {
            slab_count++;
            total_slots += (int)s->n_slots;
            used_slots += (int)s->n_used;
        }
        if (slab_count > 0)
            printf("  Class %2d (%3zuB): %d slabs, %d/%d slots used\n",
                   i, slab_sizes[i], slab_count, used_slots, total_slots);
    }
    printf("==================\n");
}