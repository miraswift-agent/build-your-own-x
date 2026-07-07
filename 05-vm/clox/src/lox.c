/*
 * clox — Error reporting implementation
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "lox.h"
#include "vm.h"

Lox lox;

void initLox(const char *source) {
    lox.source = source;
    lox.hadError = false;
}

void loxError(int line, const char *message, ...) {
    va_list args;
    va_start(args, message);
    fprintf(stderr, "[line %d] Error: ", line);
    vfprintf(stderr, message, args);
    fprintf(stderr, "\n");
    va_end(args);
    lox.hadError = true;
}

void runtimeError(const char *format, ...) {
    va_list args;
    va_start(args, format);
    fprintf(stderr, "Error: ");
    vfprintf(stderr, format, args);
    fprintf(stderr, "\n");
    va_end(args);

    for (int i = vm.frameCount - 1; i >= 0; i--) {
        CallFrame *frame = &vm.frames[i];
        ObjFunction *function = frame->closure->function;
        size_t instruction = (size_t)(frame->ip - function->chunk.code - 1);
        int line = function->chunk.lines[instruction];
        fprintf(stderr, "[line %d] in ", line);
        if (function->name == NULL) {
            fprintf(stderr, "script\n");
        } else {
            fprintf(stderr, "%s()\n", function->name->chars);
        }
    }

    longjmp(lox.errorJump, 1);
}
