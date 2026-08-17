/*
 * clox — Garbage collector
 *
 * Mark-and-sweep collector with an explicit gray stack. Roots are the
 * value stack, call frames, globals, open upvalues, and the compiler.
 */

#ifndef CLOX_GC_H
#define CLOX_GC_H

#include "common.h"
#include "object.h"
#include "value.h"

void markObject(Obj *object);
void markValue(Value value);
void markCompilerRoots(void);
void collectGarbage(void);
void freeObject(Obj *object);
void freeObjects(void);

#endif /* CLOX_GC_H */
