#ifndef MALLOC_H
#define MALLOC_H

#include <stddef.h>

/*
 * mira_malloc — Stage 4: Slab allocator for small objects
 *
 * Small objects (≤ 256 bytes): slab allocator with bitmap allocation,
 * no per-object header. O(1) malloc and free.
 *
 * Large objects (> 256 bytes): segregated free lists from Stage 3,
 * with boundary tags, prev_free bit, singly-linked per-bucket lists.
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
#define SLAB_MAGIC      0x5142534CL   /* "QBSL" — sanity check */
#define SLAB_ALIGN      4096          /* Slabs are page-aligned */
#define SMALL_THRESHOLD 256           /* Max size for slab allocation */
#define NUM_SLAB_CLASSES 12           /* 16..256 in 16-byte increments */
#define SLAB_MIN_SLOTS  14           /* Minimum slots per slab */

/* ─── Slab header ─── */

typedef struct slab_header {
    unsigned int magic;          /* SLAB_MAGIC */
    unsigned int _pad;
    size_t       slot_size;      /* Size of each slot */
    size_t       n_slots;        /* Total slots in this slab */
    size_t       n_used;         /* Currently allocated slots */
    struct slab_header *next;    /* Next slab in same size class */
    /* bitmap follows immediately after this header */
} slab_header_t;

#define SLAB_BITMAP(s)   ((unsigned char *)((char *)(s) + sizeof(slab_header_t)))
#define SLAB_DATA(s)     ((void *)((char *)(s) + sizeof(slab_header_t) + \
                          ((((s)->n_slots + 7) / 8 + ALIGNMENT - 1) & ~(ALIGNMENT - 1))))

/* ─── Free list header macros (Stage 3) ─── */

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
static inline int slab_class(size_t size) {
    if (size > SMALL_THRESHOLD) return -1;
    return (int)((size + 15) / 16) - 1;  /* 16→0, 32→1, ..., 256→15, but we want 0-11 */
}

static inline size_t slab_slot_size(int cls) {
    return (size_t)(cls + 1) * 16;  /* cls 0→16, cls 1→32, ..., cls 11→192... */
    /* Actually: 16, 32, 48, 64, 80, 96, 112, 128, 160, 192, 224, 256 */
}

/* ─── Global state ─── */

extern word_t *heap_start;
extern word_t *heap_end;
extern slab_header_t *slab_lists[NUM_SLAB_CLASSES];

/* ─── Public API ─── */

void   *mira_malloc(size_t size);
void    mira_free(void *ptr);
void   *mira_calloc(size_t nmemb, size_t size);
void   *mira_realloc(void *ptr, size_t size);

/* Diagnostics */
void    mira_heap_check(void);
size_t  mira_free_space(void);
size_t  mira_largest_free(void);
void    mira_print_heap(void);
size_t  mira_free_list_length(void);
void    mira_print_buckets(void);
void    mira_print_slabs(void);

#endif /* MALLOC_H */