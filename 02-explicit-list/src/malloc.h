#ifndef MALLOC_H
#define MALLOC_H

#include <stddef.h>

/* 
 * mira_malloc — Stage 2: Explicit free list
 * 
 * Free blocks are linked via prev/next pointers stored in the payload
 * area. Allocated blocks have only a header (8 bytes). Free blocks
 * need header (8) + prev (8) + next (8) + footer (8) = 32 bytes minimum.
 *
 * Block layout (64-bit, 8-byte alignment):
 *
 * Allocated block:           Free block:
 * +------------------+       +------------------+
 * | header (8 bytes) |       | header (8 bytes) |
 * | size|prev_free|alloc|    | size|prev_free|0  |
 * +------------------+       +------------------+
 * | payload...       |       | prev_free (8)    |
 * +------------------+       +------------------+
 *                            | next_free (8)    |
 *                            +------------------+
 *                            | (unused...)      |
 *                            +------------------+
 *                            | footer (8 bytes) |
 *                            | size|alloc=0     |
 *                            +------------------+
 *
 * prev_free bit (bit 1): indicates previous block is free (safe to
 * read its boundary tag for backward coalescing).
 *
 * LIFO policy: most recently freed block is checked first.
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

/* Free list pointer access (inside free block payload area) */
#define FREE_NEXT(h)       (*(word_t **)((char *)(h) + WORD_SIZE))

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

#endif /* MALLOC_H */