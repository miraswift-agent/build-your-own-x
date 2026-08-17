#include "lox.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

MemBlock* g_memBlocks = NULL;
LoxState g_lox;

void* loxAllocate(size_t size) {
    void* ptr = malloc(size);
    if (ptr == NULL) {
        fprintf(stderr, "Out of memory.\n");
        exit(74);
    }

    MemBlock* block = malloc(sizeof(MemBlock));
    if (block == NULL) {
        fprintf(stderr, "Out of memory.\n");
        exit(74);
    }

    block->ptr = ptr;
    block->next = g_memBlocks;
    g_memBlocks = block;

    return ptr;
}

char* loxCopyString(const char* chars, size_t length) {
    char* heapChars = loxAllocate(length + 1);
    memcpy(heapChars, chars, length);
    heapChars[length] = '\0';
    return heapChars;
}

void loxFreeAllMemory(void) {
    while (g_memBlocks != NULL) {
        MemBlock* block = g_memBlocks;
        g_memBlocks = g_memBlocks->next;
        free(block->ptr);
        free(block);
    }
}

void loxError(int line, const char* message, ...) {
    va_list args;
    va_start(args, message);
    fprintf(stderr, "[line %d] Error: ", line);
    vfprintf(stderr, message, args);
    fprintf(stderr, "\n");
    va_end(args);
    g_lox.hadParseError = true;
}

void loxRuntimeError(const char* message, ...) {
    va_list args;
    va_start(args, message);
    fprintf(stderr, "[line %d] Runtime error: ", g_lox.currentLine);
    vfprintf(stderr, message, args);
    fprintf(stderr, "\n");
    va_end(args);
    g_lox.hadRuntimeError = true;
    longjmp(g_lox.errorJump, 1);
}
