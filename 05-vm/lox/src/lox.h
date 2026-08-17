/*
 * Lox Tree-Walking Interpreter — Stage 04
 *
 * A complete implementation of the Lox language from
 * Crafting Interpreters (Part I / jlox), in C99.
 *
 * "The tree is a ladder. Each branch is a choice, and the
 *  interpreter climbs it one decision at a time."
 */

#ifndef LOX_H
#define LOX_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>

/* ============================================================
 * Memory Arena — every allocation is recorded so we can free
 * the whole heap at shutdown. Stage 4 is batch-mode only; a
 * real GC is marked TODO in the README.
 * ============================================================ */

typedef struct MemBlock {
    void* ptr;
    struct MemBlock* next;
} MemBlock;

extern MemBlock* g_memBlocks;

void* loxAllocate(size_t size);
char* loxCopyString(const char* chars, size_t length);
void  loxFreeAllMemory(void);

#define ALLOCATE(type)      ((type*)loxAllocate(sizeof(type)))
#define ALLOCATE_ARRAY(type, count) \
    ((type*)loxAllocate(sizeof(type) * (count)))
#define FREE_ARRAY(type, pointer, oldCount) \
    ((void)0)  /* all memory freed at shutdown via arena */

/* ============================================================
 * Error Reporting
 * ============================================================ */

typedef struct {
    const char* source;
    bool hadParseError;
    bool hadRuntimeError;
    int currentLine;   /* line of the expression being evaluated */
    jmp_buf errorJump;
} LoxState;

extern LoxState g_lox;

void loxError(int line, const char* message, ...);
void loxRuntimeError(const char* message, ...);

#endif /* LOX_H */
