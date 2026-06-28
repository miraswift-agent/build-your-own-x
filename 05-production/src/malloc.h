#ifndef MALLOC_H
#define MALLOC_H

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stddef.h>
#include <pthread.h>

/*
 * mira_malloc — Stage 5: Production allocator
 *
 * Thread-safe, mmap threshold, memory return to OS, introspection.
 *
 * Small objects (≤ 256 bytes): slab allocator with bitmap allocation,
 * per-class spinlocks. O(1) malloc and free.
 *
 * Medium objects (257 bytes – mmap_threshold): segregated free lists
 * with boundary tags, prev_free bit, singly-linked per-bucket lists.
 * Protected by arena spinlock.
 *
 * Large objects (≥ mmap_threshold): direct mmap/munmap. No arena
 * fragmentation from huge allocations.
 *
 * "What I cannot create, I do not understand." — Feynman
 */

typedef size_t word_t;

/* ─── Constants ─── */

#define WORD_SIZE       sizeof(word_t)
#define ALIGNMENT       WORD_SIZE
#define ALIGN(size)     (((size) + ALIGNMENT - 1) & ~(ALIGNMENT - 1))

/* Free list constants (Stage 3) */
#define MIN_BLOCK_SIZE  (ALIGN(3 * WORD_SIZE))   /* 24: hdr + next + foot */
#define NUM_SIZE_CLASSES 22

/* Slab constants */
#define SLAB_MAGIC      0x5142534CL
#define SLAB_ALIGN       4096
#define SMALL_THRESHOLD  256
#define NUM_SLAB_CLASSES 12
#define SLAB_MIN_SLOTS  14

/* Production constants */
#define MMAP_THRESHOLD  (1 << 17)   /* 128 KB — large blocks get own mmap */
#define ARENA_INIT_SIZE (1 << 20)   /* 1 MB initial arena */
#define ARENA_GROW_SIZE (1 << 16)   /* 64 KB growth increments */
#define TRIM_THRESHOLD  (1 << 18)   /* 256 KB — trim arena top if free exceeds */
#define MADVISE_CHUNK   (1 << 16)   /* 64 KB minimum for madvise DONTNEED */

/* ─── Slab header ─── */

typedef struct slab_header {
    unsigned int magic;
    unsigned int _pad;
    size_t       slot_size;
    size_t       n_slots;
    size_t       n_used;
    struct slab_header *next;    /* Next slab in same size class */
    /* bitmap follows immediately after this header */
} slab_header_t;

#define SLAB_BITMAP(s)   ((unsigned char *)((char *)(s) + sizeof(slab_header_t)))
#define SLAB_DATA(s)     ((void *)((char *)(s) + sizeof(slab_header_t) + \
                          ((((s)->n_slots + 7) / 8 + ALIGNMENT - 1) & ~(ALIGNMENT - 1))))

/* ─── Mmap block header (for large allocations) ─── */

typedef struct mmap_header {
    size_t total_size;            /* Total mmap'd size including header */
    size_t user_size;             /* Requested size (for usable_size) */
    struct mmap_header *next;     /* Linked list of mmap blocks */
    struct mmap_header *prev;
    unsigned int magic;
#define MMAP_MAGIC 0x4D4D4150L   /* "MMAP" */
} mmap_header_t;

/* ─── Free list header macros ─── */

#define GET_SIZE(h)       (*(h) & ~(ALIGNMENT - 1))
#define GET_ALLOC(h)      (*(h) & 0x1)
#define GET_PREV_FREE(h)  ((*(h) >> 1) & 0x1)
#define SET(h, size, alloc) (*(h) = (size) | (alloc))
#define SET_WITH_PREV(h, size, alloc, pf) (*(h) = (size) | (alloc) | ((pf) << 1))

#define HDR_TO_PAYLOAD(h)  ((void *)((char *)(h) + WORD_SIZE))
#define PAYLOAD_TO_HDR(p)  ((word_t *)((char *)(p) - WORD_SIZE))
#define NEXT_HDR(h)        ((word_t *)((char *)(h) + GET_SIZE(h)))
#define FOOTER(h)          ((word_t *)((char *)(h) + GET_SIZE(h) - WORD_SIZE))
#define IS_EPILOGUE(h)     (GET_SIZE(h) == 0)
#define FREE_NEXT(h)       (*(word_t **)((char *)(h) + WORD_SIZE))

/* Size class for free lists */
static inline int size_class(size_t size) {
    static const size_t classes[NUM_SIZE_CLASSES] = {
        16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128,
        160, 192, 224, 256, 320, 384, 448, 512, 1024, 2048, 4096
    };
    for (int i = 0; i < NUM_SIZE_CLASSES - 1; i++)
        if (size <= classes[i]) return i;
    return NUM_SIZE_CLASSES - 1;
}

static inline size_t class_size(int cls) {
    static const size_t classes[NUM_SIZE_CLASSES] = {
        16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128,
        160, 192, 224, 256, 320, 384, 448, 512, 1024, 2048, 4096
    };
    return classes[cls];
}

/* Slab size class index (0=16B .. 11=256B) */
static inline int slab_class_from_size(size_t size) {
    if (size > SMALL_THRESHOLD) return -1;
    /* Round up to next multiple of 16, then divide by 16, subtract 1 */
    size_t rounded = (size + 15) & ~(size_t)15;
    return (int)(rounded / 16) - 1;
}

static inline size_t slab_slot_size(int cls) {
    static const size_t sizes[NUM_SLAB_CLASSES] = {
        16, 32, 48, 64, 80, 96, 112, 128, 160, 192, 224, 256
    };
    return sizes[cls];
}

/* ─── Global state ─── */

extern word_t *heap_start;
extern word_t *heap_end;
extern slab_header_t *slab_lists[NUM_SLAB_CLASSES];
extern mmap_header_t *mmap_list;

/* ─── Public API ─── */

void   *mira_malloc(size_t size);
void    mira_free(void *ptr);
void   *mira_calloc(size_t nmemb, size_t size);
void   *mira_realloc(void *ptr, size_t size);
size_t  mira_malloc_usable_size(void *ptr);

/* Diagnostics */
void    mira_heap_check(void);
size_t  mira_free_space(void);
size_t  mira_largest_free(void);
void    mira_print_heap(void);
size_t  mira_free_list_length(void);
void    mira_print_buckets(void);
void    mira_print_slabs(void);
void    mira_malloc_stats(void);

#endif /* MALLOC_H */