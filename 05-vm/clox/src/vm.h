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

/* --- Stage 30 architecture: expose `callClosure` for natives. --- */
/* See the comment in vm.c above `callClosure` for the full contract.
 * Natives that need to invoke a user-defined Lox closure call this
 * with (closure, argCount) where args are already on the stack.
 *
 * For most native use cases, prefer `callClosureFromNative` (below),
 * which does call + run + pop-result in one step. Use `callClosure`
 * directly only if you need to set up the frame without running it. */
bool callClosure(ObjClosure *closure, int argCount);

/* Higher-level wrapper: calls callClosure(), runs the closure, pops
 * the result, and returns it. This is what natives should use.
 * See vm.c for the full contract. */
Value callClosureFromNative(ObjClosure *closure, int argCount);

typedef enum {
    INTERPRET_OK,
    INTERPRET_COMPILE_ERROR,
    INTERPRET_RUNTIME_ERROR,
    INTERPRET_EXIT  /* io_exit(code) requested termination. */
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
    /* Stage 64.0: path of the script currently being interpreted
     * (argv path or NULL for REPL). Used later for import resolution. */
    const char *scriptPath;
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

/* pathOrNull: filesystem path of this source for module resolution,
 * or NULL when running from the REPL / anonymous buffer. */
InterpretResult interpret(const char *source, const char *pathOrNull);

void collectGarbage(void);

#endif /* CLOX_VM_H */
