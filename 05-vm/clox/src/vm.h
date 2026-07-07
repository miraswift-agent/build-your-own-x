/*
 * clox — Virtual machine
 *
 * Stack-based execution engine with call frames, global variables, open
 * upvalues, and a mark-and-sweep garbage collector.
 */

#ifndef CLOX_VM_H
#define CLOX_VM_H

#include "chunk.h"
#include "object.h"
#include "table.h"
#include "value.h"

typedef enum {
    INTERPRET_OK,
    INTERPRET_COMPILE_ERROR,
    INTERPRET_RUNTIME_ERROR
} InterpretResult;

#define FRAMES_MAX 64
#define STACK_MAX  (FRAMES_MAX * 256)

typedef struct {
    ObjClosure *closure;
    uint8_t *ip;
    Value *slots;
} CallFrame;

typedef struct {
    CallFrame frames[FRAMES_MAX];
    int frameCount;

    Value stack[STACK_MAX];
    Value *stackTop;

    Table globals;
    Table strings;
    ObjUpvalue *openUpvalues;

    size_t bytesAllocated;
    size_t nextGC;

    Obj *objects;
    int grayCount;
    int grayCapacity;
    Obj **grayStack;
} VM;

extern VM vm;

void initVM(void);
void freeVM(void);
void push(Value value);
Value pop(void);
Value peek(int distance);

InterpretResult interpret(const char *source);

void collectGarbage(void);

#endif /* CLOX_VM_H */
