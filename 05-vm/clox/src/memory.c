/*
 * clox — Memory management implementation
 */

#include <stdio.h>
#include <stdlib.h>

#include "memory.h"
#include "vm.h"

void* reallocate(void *pointer, size_t oldSize, size_t newSize) {
    vm.bytesAllocated += newSize - oldSize;
    if (newSize > oldSize) {
#if DEBUG_STRESS_GC
        collectGarbage();
#else
        if (vm.bytesAllocated > vm.nextGC) {
            collectGarbage();
        }
#endif
    }

    if (newSize == 0) {
        free(pointer);
        return NULL;
    }

    void *result = realloc(pointer, newSize);
    if (result == NULL) {
        fprintf(stderr, "Out of memory.\n");
        exit(1);
    }
    return result;
}
