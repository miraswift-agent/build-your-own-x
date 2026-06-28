/*
 * Stage 1 invariant test: walks heap after every operation to catch
 * the FIRST corruption, not the cascade.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include "../src/malloc.h"

static int violations = 0;

static void check(const char *op)
{
    if (heap_start == NULL) return;

    for (word_t *h = heap_start; !IS_EPILOGUE(h); h = NEXT_HDR(h)) {
        size_t size = GET_SIZE(h);
        int alloc = GET_ALLOC(h);
        int pf = GET_PREV_FREE(h);

        /* Size must be aligned and >= MIN_BLOCK_SIZE */
        if (size < MIN_BLOCK_SIZE || (size & (ALIGNMENT - 1)) != 0) {
            printf("INVARIANT VIOLATION [%s]: block %p invalid size %zu\n",
                   op, (void *)h, size);
            violations++;
            return;
        }

        /* prev_free bit must match previous block's alloc status */
        if (h == heap_start) {
            /* First real block — prev is prologue, which is always allocated */
            if (pf != 0) {
                printf("INVARIANT VIOLATION [%s]: first block prev_free=%d but prev is prologue\n",
                       op, pf);
                violations++;
            }
        } else {
            /* Walk back to find previous block */
            word_t *p = heap_start;
            word_t *prev_blk = NULL;
            while (NEXT_HDR(p) <= h && !IS_EPILOGUE(p)) {
                prev_blk = p;
                p = NEXT_HDR(p);
            }
            if (prev_blk != NULL) {
                int prev_alloc = GET_ALLOC(prev_blk);
                if (pf != !prev_alloc) {
                    printf("INVARIANT VIOLATION [%s]: block %p prev_free=%d but prev alloc=%d\n",
                           op, (void *)h, pf, prev_alloc);
                    violations++;
                    return;
                }
            }
        }

        /* Free blocks must have matching footer */
        if (!alloc) {
            word_t *foot = FOOTER(h);
            if (GET_SIZE(foot) != size) {
                printf("INVARIANT VIOLATION [%s]: free block %p footer size mismatch (hdr=%zu foot=%zu)\n",
                       op, (void *)h, size, GET_SIZE(foot));
                violations++;
                return;
            }
            /* No two adjacent free blocks (coalescing invariant) */
            word_t *next = NEXT_HDR(h);
            if (!IS_EPILOGUE(next) && !GET_ALLOC(next)) {
                printf("INVARIANT VIOLATION [%s]: adjacent free blocks at %p and %p\n",
                       op, (void *)h, (void *)next);
                violations++;
                return;
            }
        }

        /* Check we don't walk past heap_end */
        if (NEXT_HDR(h) > heap_end) {
            printf("INVARIANT VIOLATION [%s]: block %p walks past heap_end\n",
                   op, (void *)h);
            violations++;
            return;
        }
    }
}

int main(void)
{
    printf("=== Invariant Stress Test ===\n\n");

    #define OPS 500
    #define MAX_PTRS 50
    #define MAX_SIZE 128

    void *ptrs[MAX_PTRS] = {0};
    srand(42);

    for (int i = 0; i < OPS && violations == 0; i++) {
        int slot = rand() % MAX_PTRS;

        if (ptrs[slot] == NULL) {
            size_t size = (size_t)(rand() % MAX_SIZE) + 1;
            ptrs[slot] = mira_malloc(size);
            if (ptrs[slot])
                memset(ptrs[slot], slot & 0xff, size);
            check("after malloc");
        } else {
            mira_free(ptrs[slot]);
            ptrs[slot] = NULL;
            check("after free");
        }
    }

    /* Free remaining */
    for (int i = 0; i < MAX_PTRS; i++) {
        if (ptrs[i]) {
            mira_free(ptrs[i]);
            ptrs[i] = NULL;
            check("after cleanup free");
        }
    }

    if (violations == 0) {
        printf("All invariants held! ✓\n");
        mira_print_heap();
    } else {
        printf("\n%d invariant violations found.\n", violations);
    }

    return violations;
}