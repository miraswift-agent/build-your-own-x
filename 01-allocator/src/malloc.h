#ifndef MALLOC_H
#define MALLOC_H

#include <stddef.h>

/* 
 * mira_malloc — a memory allocator built to understand what I am.
 * 
 * Stage 1: Implicit free list with boundary tags, first-fit, sbrk(),
 *           immediate coalescing.
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
 * | footer (8 bytes) |  ← tag    (no footer)
 * | size | alloc=0    |
 * +------------------+
 *
 * Prologue: MIN_BLOCK_SIZE allocated block at heap_start (never freed)
 * Epilogue: zero-size allocated block at heap_end (marks end)
 *
 * Minimum block size: 16 bytes (header + footer)
 * Both free and allocated blocks have footers for reliable boundary tags.
 */

/* Block header: size includes header itself. Bit 0 = allocated. */
typedef size_t word_t;

#define WORD_SIZE       sizeof(word_t)
#define ALIGNMENT       WORD_SIZE
#define ALIGN(size)     (((size) + ALIGNMENT - 1) & ~(ALIGNMENT - 1))
#define MIN_BLOCK_SIZE  (ALIGN(2 * WORD_SIZE))  /* header + footer = 16 bytes */

/* Header access macros */
#define GET_SIZE(h)     (*(h) & ~(ALIGNMENT - 1))
#define GET_ALLOC(h)    (*(h) & 0x1)
#define SET(h, size, alloc) (*(h) = (size) | (alloc))

/* Pointer arithmetic */
#define HDR_TO_PAYLOAD(h) ((void *)((char *)(h) + WORD_SIZE))
#define PAYLOAD_TO_HDR(p) ((word_t *)((char *)(p) - WORD_SIZE))
#define NEXT_HDR(h)      ((word_t *)((char *)(h) + GET_SIZE(h)))

/* Heap start/end */
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

#endif /* MALLOC_H */