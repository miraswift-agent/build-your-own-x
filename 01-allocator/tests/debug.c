#include <stdio.h>
#include <string.h>
#include "malloc.h"

int main(void) {
    printf("=== Debug Coalesce Test ===\n\n");
    void *a = mira_malloc(64);
    void *b = mira_malloc(64);
    void *c = mira_malloc(64);
    printf("After alloc a,b,c:\n");
    mira_print_heap();
    printf("\nFree a:\n");
    mira_free(a);
    mira_print_heap();
    printf("\nFree b:\n");
    mira_free(b);
    mira_print_heap();
    printf("\nFree c:\n");
    mira_free(c);
    mira_print_heap();
    printf("\nFree space: %zu, Largest: %zu\n", mira_free_space(), mira_largest_free());
    return 0;
}