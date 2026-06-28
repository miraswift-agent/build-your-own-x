#ifndef MALLOC_H
#define MALLOC_H

#include <stddef.h>

/* 
 * mira_malloc — Stage 3: Segregated free lists
 * 
 * Multiple size classes, each with its own free list.
 * Allocation: compute size class, check that bucket, try next
 * larger. Free: compute size class, prepend to that bucket.
 * 
 * Near O(1) for common sizes. This is what real malloc approximates.
 *
 * Block layout identical to Stage 2 (prev_free bit, singly-linked
 * free list within each bucket).
 *
 * Size classes (powers of 2, plus one large class):
 *   16, 32, 48, 64, 80, 96, 112, 128, 160, 192, 224, 256,
 *   320, 384, 448, 512, 640, 768, 896, 1024, 2048, 4096+, large
 */

typedef size_t word_t;

#define WORD_SIZE       sizeof(word_t)
#define ALIGNMENT       WORD_SIZE
#define ALIGN(size)     (((size) + ALIGNMENT - 1) & ~(ALIGNMENT - 1))
#define MIN_BLOCK_SIZE  (ALIGN(3 * WORD_SIZE))  /* header + next + footer = 24 */

/* Header access macros */
#define GET_SIZE(h)       (*(h) & ~(ALIGNMENT - 1))
#define GET_ALLOC(h)      (*(h) & 0x1)
#define GET_PREV_FREE(h)  ((*(h) >> 1) & 0x1)
#define SET(h, size, alloc) (*(h) = (size) | (alloc))
#define SET_WITH_PREV(h, size, alloc, pf) (*(h) = (size) | (alloc) | ((pf) << 1))

/* Pointer arithmetic */
#define HDR_TO_PAYLOAD(h)  ((void *)((char *)(h) + WORD_SIZE))
#define PAYLOAD_TO_HDR(p)  ((word_t *)((char *)(p) - WORD_SIZE))
#define NEXT_HDR(h)        ((word_t *)((char *)(h) + GET_SIZE(h)))
#define FOOTER(h)          ((word_t *)((char *)(h) + GET_SIZE(h) - WORD_SIZE))
#define IS_EPILOGUE(h)     (GET_SIZE(h) == 0)

/* Free list pointer (inside free block payload) */
#define FREE_NEXT(h)       (*(word_t **)((char *)(h) + WORD_SIZE))

/* Size classes */
#define NUM_SIZE_CLASSES  22

/* Get the size class index for a given block size */
static inline int size_class(size_t size) {
    /* Classes: 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128,
     *          160, 192, 224, 256, 320, 384, 448, 512, 1024, 2048, 4096+ */
    static const size_t classes[NUM_SIZE_CLASSES] = {
        16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128,
        160, 192, 224, 256, 320, 384, 448, 512, 1024, 2048, 4096
    };
    for (int i = 0; i < NUM_SIZE_CLASSES - 1; i++) {
        if (size <= classes[i])
            return i;
    }
    return NUM_SIZE_CLASSES - 1;  /* large class */
}

/* Get the minimum block size for a size class */
static inline size_t class_size(int cls) {
    static const size_t classes[NUM_SIZE_CLASSES] = {
        16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128,
        160, 192, 224, 256, 320, 384, 448, 512, 1024, 2048, 4096
    };
    return classes[cls];
}

/* Heap start/end (global) */
extern word_t *heap_start;
extern word_t *heap_end;

/* Public API */
void   *mira_malloc(size_t size);
void    mira_free(void *ptr);
void   *mira_calloc(size_t nmemb, size_t size);
void   *mira_realloc(void *ptr, size_t size);

/* Debug/diagnostics */
void    mira_heap_check(void);
size_t  mira_free_space(void);
size_t  mira_largest_free(void);
void    mira_print_heap(void);
size_t  mira_free_list_length(void);
void    mira_print_buckets(void);

#endif /* MALLOC_H */