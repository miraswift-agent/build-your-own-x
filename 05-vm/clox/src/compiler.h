/*
 * clox — Single-pass bytecode compiler
 *
 * A Pratt parser that emits bytecode directly into a Chunk while scanning
 * the source from left to right. There is no separate AST.
 */

#ifndef CLOX_COMPILER_H
#define CLOX_COMPILER_H

#include "object.h"
#include "scanner.h"
#include "vm.h"

ObjFunction *compile(const char *source);
void markCompilerRoots(void);

#endif /* CLOX_COMPILER_H */
