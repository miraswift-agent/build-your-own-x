/*
 * Lantern VM — Stage 3: Compiler
 *
 * A minimal C-like source language that compiles to Lantern Bytecode.
 *
 * "Source is where the machine becomes readable to humans."
 */

#ifndef COMPILER_H
#define COMPILER_H

#include "lantern.h"

typedef struct AstNode AstNode;
typedef struct AstAlloc AstAlloc;

typedef struct {
    char *source;
    Assembler assembler;
    int error;
    char error_msg[256];

    /* Internal — freed by compiler_free. */
    AstNode *program;
    AstAlloc *ast_allocs;
} Compiler;

void compiler_init(Compiler *c, const char *source);
bool compiler_compile(Compiler *c);
Chunk *compiler_get_main(Compiler *c);
const char *compiler_error_string(Compiler *c);
void compiler_free(Compiler *c);

#endif /* COMPILER_H */
