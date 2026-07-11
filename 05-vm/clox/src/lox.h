/*
 * clox — Error reporting and shared state
 *
 * Reports compile-time and runtime errors with line numbers. Runtime
 * errors use a longjmp to unwind back to the top-level interpret() call.
 */

#ifndef CLOX_LOX_H
#define CLOX_LOX_H

#include <setjmp.h>
#include <stdbool.h>

typedef struct {
    const char *source;
    bool hadError;
    jmp_buf errorJump;
} Lox;

extern Lox lox;

void initLox(const char *source);
void loxError(int line, const char *message, ...);
void runtimeError(const char *format, ...);

#endif /* CLOX_LOX_H */
