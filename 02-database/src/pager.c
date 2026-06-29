/*
 * pager.c — Page-based storage manager implementation
 *
 * Handles page I/O, allocation via free list, and file growth.
 * Supports mmap for fast access when available.
 */

#include "pager.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <errno.h>

/* Try to mmap the entire file for fast page access */
static int pager_try_mmap(Pager *p)
{
    struct stat st;
    if (fstat(p->fd, &st) != 0) return -1;

    size_t file_size = (size_t)st.st_size;
    if (file_size == 0) {
        p->use_mmap = 0;
        p->mmap_ptr = NULL;
        p->mmap_len = 0;
        return 0;
    }

    /* Extend mmap length to cover all pages, round up to page boundary */
    size_t mmap_len = (size_t)p->page_count * PAGE_SIZE;
    if (mmap_len == 0) mmap_len = PAGE_SIZE * 2; /* at least header + free list */

    void *ptr = mmap(NULL, mmap_len, PROT_READ | PROT_WRITE, MAP_SHARED, p->fd, 0);
    if (ptr == MAP_FAILED) {
        p->use_mmap = 0;
        p->mmap_ptr = NULL;
        return 0; /* non-fatal, fall back to read/write */
    }

    p->mmap_ptr = (uint8_t *)ptr;
    p->mmap_len = mmap_len;
    p->use_mmap = 1;
    return 0;
}

/* Unmap if currently mapped */
static void pager_unmmap(Pager *p)
{
    if (p->mmap_ptr && p->mmap_ptr != MAP_FAILED) {
        munmap(p->mmap_ptr, p->mmap_len);
    }
    p->mmap_ptr = NULL;
    p->mmap_len = 0;
    p->use_mmap = 0;
}

/* Ensure file is large enough for page_num */
static int pager_ensure_size(Pager *p, uint32_t page_num)
{
    off_t needed = (off_t)(page_num + 1) * PAGE_SIZE;
    off_t current = lseek(p->fd, 0, SEEK_END);
    if (current < 0) return -1;

    if (current < needed) {
        /* Extend file with zeros */
        if (ftruncate(p->fd, needed) != 0) return -1;
    }
    return 0;
}

int pager_open(Pager *p, const char *filename)
{
    memset(p, 0, sizeof(*p));

    /* Check if file exists and has content */
    int exists = 0;
    struct stat st;
    if (stat(filename, &st) == 0 && st.st_size >= PAGE_SIZE * 2) {
        exists = 1;
    }

    p->fd = open(filename, O_RDWR | O_CREAT, 0644);
    if (p->fd < 0) return -1;

    p->page_cache = (uint8_t *)malloc(PAGE_SIZE);
    if (!p->page_cache) {
        close(p->fd);
        return -1;
    }

    if (exists) {
        /* Read existing header */
        if (pager_read_header(p) != 0) {
            free(p->page_cache);
            close(p->fd);
            return -1;
        }
    } else {
        /* New file — initialize header and free list */
        p->page_count = 2; /* header + free list head */
        p->free_list = 0;  /* no free pages yet */

        if (pager_ensure_size(p, 1) != 0) {
            free(p->page_cache);
            close(p->fd);
            return -1;
        }

        /* Write empty header (will be filled by btree_open) */
        memset(p->page_cache, 0, PAGE_SIZE);
        if (pager_write_page(p, 0, p->page_cache) != 0) {
            free(p->page_cache);
            close(p->fd);
            return -1;
        }

        /* Write empty free list head */
        memset(p->page_cache, 0, PAGE_SIZE);
        if (pager_write_page(p, 1, p->page_cache) != 0) {
            free(p->page_cache);
            close(p->fd);
            return -1;
        }
    }

    /* Try mmap for fast access */
    pager_try_mmap(p);

    return 0;
}

int pager_close(Pager *p)
{
    if (p->use_mmap) {
        /* Sync mmap'd data */
        if (p->mmap_ptr) msync(p->mmap_ptr, p->mmap_len, MS_SYNC);
        pager_unmmap(p);
    }

    if (p->page_cache) {
        free(p->page_cache);
        p->page_cache = NULL;
    }

    if (p->fd >= 0) {
        fsync(p->fd);
        close(p->fd);
        p->fd = -1;
    }

    return 0;
}

int pager_read_page(Pager *p, uint32_t page_num, uint8_t *buf)
{
    if (page_num >= p->page_count) return -1;

    if (p->use_mmap && p->mmap_ptr) {
        memcpy(buf, p->mmap_ptr + (size_t)page_num * PAGE_SIZE, PAGE_SIZE);
        return 0;
    }

    off_t offset = (off_t)page_num * PAGE_SIZE;
    ssize_t n = pread(p->fd, buf, PAGE_SIZE, offset);
    if (n != PAGE_SIZE) return -1;
    return 0;
}

int pager_write_page(Pager *p, uint32_t page_num, const uint8_t *buf)
{
    /* Ensure we have room — if mmap'd, need to remap */
    if (page_num >= p->page_count) {
        /* Grow the file */
        if (pager_ensure_size(p, page_num) != 0) return -1;
        p->page_count = page_num + 1;

        if (p->use_mmap) {
            /* Remap to include new page */
            pager_unmmap(p);
            pager_try_mmap(p);
        }
    }

    if (p->use_mmap && p->mmap_ptr) {
        memcpy(p->mmap_ptr + (size_t)page_num * PAGE_SIZE, buf, PAGE_SIZE);
        return 0;
    }

    off_t offset = (off_t)page_num * PAGE_SIZE;
    ssize_t n = pwrite(p->fd, buf, PAGE_SIZE, offset);
    if (n != PAGE_SIZE) return -1;
    return 0;
}

uint32_t pager_allocate(Pager *p)
{
    /* Check free list first */
    if (p->free_list != 0) {
        uint32_t free_page = p->free_list;
        uint8_t buf[PAGE_SIZE];

        if (pager_read_page(p, free_page, buf) != 0) return 0;

        FreePageEntry *entry = (FreePageEntry *)buf;
        p->free_list = entry->next_free;

        /* Update free list head on disk (page 1) */
        memset(buf, 0, PAGE_SIZE);
        entry = (FreePageEntry *)buf;
        entry->next_free = p->free_list;
        if (pager_write_page(p, 1, buf) != 0) return 0;

        return free_page;
    }

    /* No free pages — extend the file */
    uint32_t new_page = p->page_count;
    if (pager_ensure_size(p, new_page) != 0) return 0;

    /* Zero the new page */
    uint8_t zero_page[PAGE_SIZE];
    memset(zero_page, 0, PAGE_SIZE);

    p->page_count = new_page + 1;

    if (p->use_mmap) {
        pager_unmmap(p);
    }

    if (pager_ensure_size(p, new_page) != 0) return 0;

    /* Write zeros */
    if (pager_write_page(p, new_page, zero_page) != 0) {
        p->page_count = new_page; /* rollback */
        return 0;
    }

    /* Update header page count */
    uint8_t hdr[PAGE_SIZE];
    if (pager_read_page(p, 0, hdr) != 0) return 0;
    HeaderPage *hp = (HeaderPage *)hdr;
    hp->page_count = p->page_count;
    if (pager_write_page(p, 0, hdr) != 0) return 0;

    if (p->use_mmap || p->mmap_ptr) {
        pager_try_mmap(p);
    }

    return new_page;
}

void pager_free(Pager *p, uint32_t page_num)
{
    if (page_num == 0) return; /* never free header */

    /* Zero the page and link it into the free list */
    uint8_t buf[PAGE_SIZE];
    memset(buf, 0, PAGE_SIZE);

    FreePageEntry *entry = (FreePageEntry *)buf;
    entry->next_free = p->free_list;

    /* Write the freed page */
    pager_write_page(p, page_num, buf);

    /* Update free list head */
    p->free_list = page_num;

    /* Update page 1 (free list head on disk) */
    memset(buf, 0, PAGE_SIZE);
    entry = (FreePageEntry *)buf;
    entry->next_free = p->free_list;
    pager_write_page(p, 1, buf);
}

int pager_write_header(Pager *p, uint32_t order, uint32_t root_page)
{
    uint8_t buf[PAGE_SIZE];
    memset(buf, 0, PAGE_SIZE);

    HeaderPage *hdr = (HeaderPage *)buf;
    hdr->magic = MAGIC_NUMBER;
    hdr->order = order;
    hdr->page_count = p->page_count;
    hdr->root_page = root_page;
    hdr->free_list = p->free_list;

    if (pager_write_page(p, 0, buf) != 0) return -1;

    /* Update cached values */
    p->page_count = hdr->page_count;
    p->free_list = hdr->free_list;

    return 0;
}

int pager_read_header(Pager *p)
{
    uint8_t buf[PAGE_SIZE];

    /* Read directly from file — don't rely on mmap yet */
    ssize_t n = pread(p->fd, buf, PAGE_SIZE, 0);
    if (n != PAGE_SIZE) return -1;

    HeaderPage *hdr = (HeaderPage *)buf;

    if (hdr->magic != MAGIC_NUMBER) {
        fprintf(stderr, "pager_read_header: bad magic 0x%08X\n", hdr->magic);
        return -1;
    }

    p->page_count = hdr->page_count;
    p->free_list = hdr->free_list;

    /* Determine actual file size */
    struct stat st;
    if (fstat(p->fd, &st) == 0) {
        uint32_t file_pages = (uint32_t)(st.st_size / PAGE_SIZE);
        if (file_pages > p->page_count) {
            p->page_count = file_pages;
        }
    }

    return 0;
}