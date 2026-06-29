/*
 * pager.h — Page-based storage manager for B-tree
 *
 * Manages fixed-size pages (4096 bytes) in a file.
 * Page 0: Header (magic, order, page count, root page)
 * Page 1: Free list head
 * Page 2+: Data pages (B-tree nodes or overflow)
 */

#ifndef PAGER_H
#define PAGER_H

#include <stddef.h>
#include <stdint.h>

#define PAGE_SIZE    4096
#define MAGIC_NUMBER 0xB7EE1111  /* "BTEE" in hex style */

/* Header page layout (page 0) */
typedef struct {
    uint32_t magic;
    uint32_t order;        /* min degree t */
    uint32_t page_count;   /* total pages allocated */
    uint32_t root_page;    /* root node page number */
    uint32_t free_list;    /* first free page (page 1 initially) */
    uint32_t reserved[3];  /* future use */
} HeaderPage;

/* Free list entry — stored at the start of a free page */
typedef struct {
    uint32_t next_free;    /* next free page, 0 = end of list */
    uint32_t padding;      /* align to 8 bytes */
} FreePageEntry;

/* Pager handle */
typedef struct Pager {
    int       fd;          /* file descriptor */
    uint32_t  page_count;  /* total pages in file */
    uint32_t  free_list;   /* head of free list */
    uint8_t  *mmap_ptr;    /* mmap'd region if available */
    size_t    mmap_len;    /* length of mmap'd region */
    int       use_mmap;    /* whether mmap is active */
    uint8_t  *page_cache;  /* single-page buffer for non-mmap I/O */
} Pager;

/*
 * pager_open — Open or create a database file.
 * If the file is new, initializes header (page 0) and free list (page 1).
 * Returns 0 on success, -1 on error.
 */
int pager_open(Pager *p, const char *filename);

/*
 * pager_close — Flush and close the database file.
 * Writes header back if needed. Returns 0 on success.
 */
int pager_close(Pager *p);

/*
 * pager_read_page — Read page number `page_num` into `buf`.
 * `buf` must be at least PAGE_SIZE bytes.
 * Returns 0 on success, -1 on error.
 */
int pager_read_page(Pager *p, uint32_t page_num, uint8_t *buf);

/*
 * pager_write_page — Write `buf` (PAGE_SIZE bytes) to page number `page_num`.
 * Returns 0 on success, -1 on error.
 */
int pager_write_page(Pager *p, uint32_t page_num, const uint8_t *buf);

/*
 * pager_allocate — Allocate a new page. Returns the page number,
 * or 0 on error (page 0 is never allocated).
 * Checks free list first, then extends the file.
 */
uint32_t pager_allocate(Pager *p);

/*
 * pager_free — Return a page to the free list.
 * Adds it to the head of the singly-linked free list.
 */
void pager_free(Pager *p, uint32_t page_num);

/*
 * pager_write_header — Write the header page from current pager state.
 */
int pager_write_header(Pager *p, uint32_t order, uint32_t root_page);

/*
 * pager_read_header — Read the header page and populate pager state.
 * Returns 0 on success, -1 on error or bad magic.
 */
int pager_read_header(Pager *p);

#endif /* PAGER_H */