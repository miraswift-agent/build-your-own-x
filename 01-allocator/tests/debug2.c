#include <stdio.h>
#include <string.h>
#include "src/malloc.h"

int main(void) {
    printf("=== Pointer Debug ===\n\n");
    printf("Before any alloc:\n");
    
    void *a = mira_malloc(64);
    printf("a = %p, hdr = %p\n", a, (char*)a - 8);
    mira_print_heap();
    
    printf("\nFree a:\n");
    mira_free(a);
    mira_print_heap();
    
    return 0;
}