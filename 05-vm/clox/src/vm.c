/*
 * clox — Virtual machine implementation
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "chunk.h"
#include "common.h"
#include "compiler.h"
#include "debug.h"
#include "gc.h"
#include "lox.h"
#include "memory.h"
#include "native.h"
#include "object.h"
#include "table.h"
#include "vm.h"

/* Stage 11: io_exit() flags. Defined here (not in main.c) so the
 * test binaries (which don't link main.c) can find them. main.c
 * and native.c declare them as extern. */
int g_exitRequested = 0;
int g_exitCode = 0;
#include "value.h"
#include "vm.h"

VM vm;

static void resetStack(void) {
    vm.stackTop = vm.stack;
    vm.frameCount = 0;
    vm.openUpvalues = NULL;
}

void initVM(void) {
    resetStack();
    vm.objects = NULL;
    vm.bytesAllocated = 0;
    vm.nextGC = 1024;

    vm.grayCount = 0;
    vm.grayCapacity = 0;
    vm.grayStack = NULL;

    initTable(&vm.globals);
    initTable(&vm.strings);

    defineNatives();
}

void freeVM(void) {
    freeTable(&vm.globals);
    freeTable(&vm.strings);
    freeObjects();
}

void push(Value value) {
    *vm.stackTop = value;
    vm.stackTop++;
}

Value pop(void) {
    vm.stackTop--;
    return *vm.stackTop;
}

Value peek(int distance) {
    return vm.stackTop[-1 - distance];
}

static bool callValue(Value callee, int argCount);

/* --- Stage 30 architecture: expose `callClosure` to native.c. --- */
/* The native function signature is `Value (*)(int argCount, Value *args)`
 * where `args` points to the first argument on the VM stack (the same
 * position the VM's OP_CALL handler uses). To invoke a user-defined
 * Lox closure from a native (e.g., array_filter's predicate), we need
 * the same machinery the OP_CALL handler uses. That machinery is the
 * `callClosure` function below.
 *
 * NOTE: Named `callClosure` (not `call`) because the compiler has its
 * own static `call(bool canAssign)` for parsing call expressions. The
 * two are unrelated; the names are separate to avoid collision.
 *
 * Promoting `callClosure` from static to public is the smallest change
 * that enables natives to invoke user code. The contract:
 *   - args must point to the first arg on the VM stack (native's args)
 *   - argCount must match closure->function->arity
 *   - args[0..argCount-1] are the call's arguments
 *   - On success, the result is pushed onto the stack and callClosure() returns true
 *   - On runtime error (arity mismatch, stack overflow, callee-side error),
 *     runtimeError() does longjmp and the callClosure() return is unreachable.
 *     (Same shape as OP_CALL in the interpreter.)
 *
 * This is the first public hook for user-code dispatch from a native.
 * The architecture: `callValue` is still static (used by OP_CALL and the
 * `callClosure` function), `callClosure` is public (used by natives that
 * need to invoke a closure with args already on the stack). */
bool callClosure(ObjClosure *closure, int argCount);

/* `run()` is the interpreter loop. Forward-declared here so natives
 * that have called `callClosure()` can actually run the closure's
 * bytecodes. The pattern: callClosure(closure, argCount) sets up
 * the new frame; run() executes the closure's bytecodes until the
 * closure returns; the result is on top of the stack when run()
 * returns to the native.
 *
 * On runtime error inside the closure, runtimeError() longjmp's and
 * run()'s return is unreachable (same as OP_CALL). */
static InterpretResult run(void);

/* `vmNativeTargetDepth` is the frameCount depth at which run() should
 * stop (returning INTERPRET_OK) instead of continuing into the outer
 * caller's bytecodes. Used by callClosureFromNative to run a closure
 * to completion and then return control to the native, not to the
 * outer script. -1 means "no target" (run until frameCount==0). */
static int vmNativeTargetDepth = -1;
static ObjUpvalue *captureUpvalue(Value *local);
static void closeUpvalues(Value *last);
static void defineMethod(ObjString *name);

bool callClosure(ObjClosure *closure, int argCount) {
    if (argCount != closure->function->arity) {
        runtimeError("Expected %d arguments but got %d.",
                     closure->function->arity, argCount);
        return false;
    }

    if (vm.frameCount == FRAMES_MAX) {
        runtimeError("Stack overflow.");
        return false;
    }

    CallFrame *frame = &vm.frames[vm.frameCount++];
    frame->closure = closure;
    frame->ip = closure->function->chunk.code;
    frame->slots = vm.stackTop - argCount - 1;
    /* Stage 30: do NOT call run() here. The function only sets up
     * the frame; the caller is responsible for running the bytecodes.
     *
     * The OP_CALL bytecode path: callValue() calls callClosure() to
     * push the new frame, then returns true. The OP_CALL handler
     * continues to the next opcode in the same run() loop iteration;
     * the new frame is at vm.frames[vm.frameCount - 1], so the next
     * run() loop iteration picks it up automatically.
     *
     * The native path (callClosureFromNative): callClosure() pushes
     * the new frame, then the wrapper sets a target depth and calls
     * run() explicitly. run() processes the new frame; when the
     * closure returns and frameCount drops to the target, OP_RETURN
     * detects the target and returns INTERPRET_OK.
     *
     * The earlier draft of this function called run() here, which
     * works for the OP_CALL path (it just runs the new frame and
     * continues into the outer caller's bytecodes, which is what
     * the OP_CALL handler would do anyway) but BREAKS the native
     * path (the recursive run() consumes the outer caller's
     * bytecodes too, so when the wrapper tries to continue the
     * outer script, the script has already finished). */
    return true;
}

static bool callValue(Value callee, int argCount) {
    if (IS_OBJ(callee)) {
        switch (OBJ_TYPE(callee)) {
            case OBJ_BOUND_METHOD: {
                ObjBoundMethod *bound = AS_BOUND_METHOD(callee);
                vm.stackTop[-argCount - 1] = bound->receiver;
                return callClosure(bound->method, argCount);
            }
            case OBJ_CLASS: {
                ObjClass *klass = AS_CLASS(callee);
                vm.stackTop[-argCount - 1] = OBJ_VAL(newInstance(klass));
                Value initializer;
                if (tableGet(&klass->methods, copyString("init", 4), &initializer)) {
                    return callClosure(AS_CLOSURE(initializer), argCount);
                } else if (argCount != 0) {
                    runtimeError("Expected 0 arguments but got %d.", argCount);
                    return false;
                }
                return true;
            }
            case OBJ_CLOSURE:
                return callClosure(AS_CLOSURE(callee), argCount);
            case OBJ_NATIVE: {
                NativeFn native = AS_NATIVE(callee);
                Value result = native(argCount, vm.stackTop - argCount);
                vm.stackTop -= argCount + 1;
                push(result);
                return true;
            }
            default:
                break;
        }
    }
    runtimeError("Can only call functions and classes.");
    return false;
}

static bool bindMethod(ObjClass *klass, ObjString *name) {
    Value method;
    if (!tableGet(&klass->methods, name, &method)) {
        runtimeError("Undefined property '%s'.", name->chars);
        return false;
    }

    ObjBoundMethod *bound = newBoundMethod(peek(0), AS_CLOSURE(method));
    pop();
    push(OBJ_VAL(bound));
    return true;
}

static ObjUpvalue *captureUpvalue(Value *local) {
    ObjUpvalue *prevUpvalue = NULL;
    ObjUpvalue *upvalue = vm.openUpvalues;
    while (upvalue != NULL && upvalue->location > local) {
        prevUpvalue = upvalue;
        upvalue = upvalue->next;
    }

    if (upvalue != NULL && upvalue->location == local) {
        return upvalue;
    }

    ObjUpvalue *createdUpvalue = newUpvalue(local);
    createdUpvalue->next = upvalue;
    if (prevUpvalue == NULL) {
        vm.openUpvalues = createdUpvalue;
    } else {
        prevUpvalue->next = createdUpvalue;
    }

    return createdUpvalue;
}

static void closeUpvalues(Value *last) {
    while (vm.openUpvalues != NULL &&
           vm.openUpvalues->location >= last) {
        ObjUpvalue *upvalue = vm.openUpvalues;
        upvalue->closed = *upvalue->location;
        upvalue->location = &upvalue->closed;
        vm.openUpvalues = upvalue->next;
    }
}

static void defineMethod(ObjString *name) {
    Value method = peek(0);
    ObjClass *klass = AS_CLASS(peek(1));
    tableSet(&klass->methods, name, method);
    pop();
}

static bool isFalsey(Value value) {
    return IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value));
}

static void concatenate(void) {
    ObjString *b = AS_STRING(peek(0));
    ObjString *a = AS_STRING(peek(1));

    int length = a->length + b->length;
    char *chars = ALLOCATE(char, length + 1);
    memcpy(chars, a->chars, a->length);
    memcpy(chars + a->length, b->chars, b->length);
    chars[length] = '\0';

    ObjString *result = takeString(chars, length);
    pop();
    pop();
    push(OBJ_VAL(result));
}

static InterpretResult run(void) {
    CallFrame *frame = &vm.frames[vm.frameCount - 1];

#define READ_BYTE() (*frame->ip++)
#define READ_SHORT() (frame->ip += 2, \
    (uint16_t)((frame->ip[-2] << 8) | frame->ip[-1]))
#define READ_CONSTANT() (frame->closure->function->chunk.constants.values[READ_BYTE()])
#define READ_STRING() AS_STRING(READ_CONSTANT())
#define BINARY_OP(valueType, op) \
    do { \
        if (!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1))) { \
            runtimeError("Operands must be numbers."); \
            return INTERPRET_RUNTIME_ERROR; \
        } \
        double b = AS_NUMBER(pop()); \
        double a = AS_NUMBER(pop()); \
        push(valueType(a op b)); \
    } while (false)

    for (;;) {
        /* Stage 11: io_exit() sets g_exitRequested from a native; the
         * VM loop checks it on every tick and bails out cleanly. The
         * check is at the top (not bottom) so a long native sequence
         * ends as soon as the next instruction boundary hits. */
        if (g_exitRequested) return INTERPRET_EXIT;

#if DEBUG_TRACE_EXEC
        printf("          ");
        for (Value *slot = vm.stack; slot < vm.stackTop; slot++) {
            printf("[ ");
            printValue(*slot);
            printf(" ]");        }
        printf("\n");
        disassembleInstruction(&frame->closure->function->chunk,
            (int)(frame->ip - frame->closure->function->chunk.code));
#endif
        uint8_t instruction;
        switch (instruction = READ_BYTE()) {
            case OP_CONSTANT: {
                Value constant = READ_CONSTANT();
                push(constant);
                break;
            }
            case OP_NIL:      push(NIL_VAL); break;
            case OP_TRUE:     push(BOOL_VAL(true)); break;
            case OP_FALSE:    push(BOOL_VAL(false)); break;
            case OP_POP:      pop(); break;
            case OP_GET_LOCAL: {
                uint8_t slot = READ_BYTE();
                push(frame->slots[slot]);
                break;
            }
            case OP_SET_LOCAL: {
                uint8_t slot = READ_BYTE();
                frame->slots[slot] = peek(0);
                break;
            }
            case OP_GET_GLOBAL: {
                ObjString *name = READ_STRING();
                Value value;
                if (!tableGet(&vm.globals, name, &value)) {
                    runtimeError("Undefined variable '%s'.", name->chars);
                    return INTERPRET_RUNTIME_ERROR;
                }
                push(value);
                break;
            }
            case OP_DEFINE_GLOBAL: {
                ObjString *name = READ_STRING();
                tableSet(&vm.globals, name, peek(0));
                pop();
                break;
            }
            case OP_SET_GLOBAL: {
                ObjString *name = READ_STRING();
                if (tableSet(&vm.globals, name, peek(0))) {
                    tableDelete(&vm.globals, name);
                    runtimeError("Undefined variable '%s'.", name->chars);
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }
            case OP_GET_UPVALUE: {
                uint8_t slot = READ_BYTE();
                push(*frame->closure->upvalues[slot]->location);
                break;
            }
            case OP_SET_UPVALUE: {
                uint8_t slot = READ_BYTE();
                *frame->closure->upvalues[slot]->location = peek(0);
                break;
            }
            case OP_GET_PROPERTY: {
                if (!IS_INSTANCE(peek(0))) {
                    runtimeError("Only instances have properties.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                ObjInstance *instance = AS_INSTANCE(peek(0));
                ObjString *name = READ_STRING();

                Value value;
                if (tableGet(&instance->fields, name, &value)) {
                    pop();
                    push(value);
                    break;
                }

                if (!bindMethod(instance->klass, name)) {
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }
            case OP_SET_PROPERTY: {
                if (!IS_INSTANCE(peek(1))) {
                    runtimeError("Only instances have fields.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                ObjInstance *instance = AS_INSTANCE(peek(1));
                tableSet(&instance->fields, READ_STRING(), peek(0));
                Value value = pop();
                pop();
                push(value);
                break;
            }
            case OP_GET_SUPER: {
                ObjString *name = READ_STRING();
                ObjClass *superclass = AS_CLASS(pop());

                if (!bindMethod(superclass, name)) {
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }
            case OP_EQUAL: {
                Value b = pop();
                Value a = pop();
                push(BOOL_VAL(valuesEqual(a, b)));
                break;
            }
            case OP_GREATER:  BINARY_OP(BOOL_VAL, >); break;
            case OP_LESS:     BINARY_OP(BOOL_VAL, <); break;
            case OP_ADD: {
                if (IS_STRING(peek(0)) && IS_STRING(peek(1))) {
                    concatenate();
                } else if (IS_NUMBER(peek(0)) && IS_NUMBER(peek(1))) {
                    double b = AS_NUMBER(pop());
                    double a = AS_NUMBER(pop());
                    push(NUMBER_VAL(a + b));
                } else {
                    runtimeError("Operands must be two numbers or two strings.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }
            case OP_SUBTRACT: BINARY_OP(NUMBER_VAL, -); break;
            case OP_MULTIPLY: BINARY_OP(NUMBER_VAL, *); break;
            case OP_DIVIDE:   BINARY_OP(NUMBER_VAL, /); break;
            case OP_NOT:
                push(BOOL_VAL(isFalsey(pop())));
                break;
            case OP_NEGATE:
                if (!IS_NUMBER(peek(0))) {
                    runtimeError("Operand must be a number.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                push(NUMBER_VAL(-AS_NUMBER(pop())));
                break;
            case OP_PRINT: {
                printValue(pop());
                printf("\n");
                break;
            }
            case OP_JUMP: {
                uint16_t offset = READ_SHORT();
                frame->ip += offset;
                break;
            }
            case OP_JUMP_IF_FALSE: {
                uint16_t offset = READ_SHORT();
                if (isFalsey(peek(0))) frame->ip += offset;
                break;
            }
            case OP_LOOP: {
                uint16_t offset = READ_SHORT();
                frame->ip -= offset;
                break;
            }
            case OP_CALL: {
                int argCount = READ_BYTE();
                if (!callValue(peek(argCount), argCount)) {
                    return INTERPRET_RUNTIME_ERROR;
                }
                frame = &vm.frames[vm.frameCount - 1];
                break;
            }
            case OP_CLOSURE: {
                ObjFunction *function = AS_FUNCTION(READ_CONSTANT());
                ObjClosure *closure = newClosure(function);
                push(OBJ_VAL(closure));
                for (int i = 0; i < closure->upvalueCount; i++) {
                    uint8_t isLocal = READ_BYTE();
                    uint8_t index = READ_BYTE();
                    if (isLocal) {
                        closure->upvalues[i] =
                            captureUpvalue(frame->slots + index);
                    } else {
                        closure->upvalues[i] = frame->closure->upvalues[index];
                    }
                }
                break;
            }
            case OP_CLOSE_UPVALUE:
                closeUpvalues(vm.stackTop - 1);
                pop();
                break;
            case OP_INDEX_SET: {
                /* Stage 12b-iii: write a[i] = v. Stack: ..., array, index, value.
                 * Pop the value (top), pop the index, peek the array,
                 * validate it's an OBJ_ARRAY, bounds-check, then
                 * arrayWrite(). No push: assignment is a statement,
                 * not an expression. */
                Value value = pop();
                Value indexValue = pop();
                if (!IS_NUMBER(indexValue)) {
                    runtimeError("Array index must be a number.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                int index = (int)AS_NUMBER(indexValue);
                Value arrayValue = peek(0);
                if (!IS_ARRAY(arrayValue)) {
                    runtimeError("Only arrays can be indexed.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                ObjArray *array = AS_ARRAY(arrayValue);
                if (index < 0 || index >= array->count) {
                    runtimeError("Array index %d out of bounds (length %d).",
                                 index, array->count);
                    return INTERPRET_RUNTIME_ERROR;
                }
                arrayWrite(array, index, value);
                /* Pop the array, push the value back as the
                 * expression result, so that the assignment
                 * expression itself has a value. Same pattern as
                 * OP_SET_LOCAL/OP_SET_GLOBAL: leave the assigned
                 * value on the stack. */
                pop();  /* the array */
                push(value);
                break;
            }
            case OP_INDEX_GET: {
                /* Stage 12b-ii: read array[i]. Stack: ..., array, index.
                 * Pop the index (top), validate it's a number, then peek
                 * the array and bounds-check, then push the element. */
                Value indexValue = pop();
                if (!IS_NUMBER(indexValue)) {
                    runtimeError("Array index must be a number.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                int index = (int)AS_NUMBER(indexValue);
                Value arrayValue = peek(0);
                if (!IS_ARRAY(arrayValue)) {
                    runtimeError("Only arrays can be indexed.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                ObjArray *array = AS_ARRAY(arrayValue);
                if (index < 0 || index >= array->count) {
                    runtimeError("Array index %d out of bounds (length %d).",
                                 index, array->count);
                    return INTERPRET_RUNTIME_ERROR;
                }
                Value element = arrayRead(array, index);
                pop();  /* the array */
                push(element);
                break;
            }
            case OP_ARRAY: {
                /* Stage 12b-i: build an ObjArray from the top N stack
                 * values. Operand (read below) is the element count.
                 * The values are below the protective push of the
                 * array itself: stackTop is one above the array, and
                 * the source values are stackTop[-count..-1].
                 * We allocate the array with capacity == count, fill
                 * it from bottom (first element, stackTop[-count]) to
                 * top (last element, stackTop[-1]), then set
                 * array->count, pop the protective push and the
                 * source values, and push the array as the single
                 * result. The push(OBJ_VAL) is GC-protective: the
                 * arrayWrite calls could otherwise collect the
                 * partially-built array. Note: arrayWrite writes
                 * elements[index] but does NOT increment count (the
                 * Stage 12a natives use arrayPush for that). For a
                 * literal, we know the final count, so we set it
                 * explicitly after the fill loop. */
                uint8_t count = READ_BYTE();
                ObjArray *array = newArray(count);
                push(OBJ_VAL(array));
                for (uint8_t i = 0; i < count; i++) {
                    arrayWrite(array, i, peek(count - i));
                }
                array->count = count;
                pop();  /* the protective push */
                for (uint8_t i = 0; i < count; i++) {
                    pop();  /* the source values */
                }
                push(OBJ_VAL(array));
                break;
            }
            case OP_RETURN: {
                Value result = pop();
                closeUpvalues(frame->slots);
                vm.frameCount--;
                if (vm.frameCount == 0) {
                    pop();
                    return INTERPRET_OK;
                }

                vm.stackTop = frame->slots;
                push(result);
                /* Stage 30: if a native set a target depth and we've
                 * reached it, return to the native instead of falling
                 * through into the outer caller's bytecodes. The
                 * target depth is the frameCount BEFORE the closure
                 * was called; when the closure returns and frameCount
                 * drops to that depth, we're back at the native's
                 * caller. */
                if (vm.frameCount == vmNativeTargetDepth) {
                    vmNativeTargetDepth = -1;  /* clear for next time */
                    return INTERPRET_OK;
                }
                frame = &vm.frames[vm.frameCount - 1];
                break;
            }
            case OP_CLASS:
                push(OBJ_VAL(newClass(READ_STRING())));
                break;
            case OP_INHERIT: {
                Value superclass = peek(1);
                if (!IS_CLASS(superclass)) {
                    runtimeError("Superclass must be a class.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                ObjClass *subclass = AS_CLASS(peek(0));
                tableAddAll(&AS_CLASS(superclass)->methods, &subclass->methods);
                pop(); /* Subclass. */
                break;
            }
            case OP_METHOD:
                defineMethod(READ_STRING());
                break;
        }
    }

#undef READ_BYTE
#undef READ_SHORT
#undef READ_CONSTANT
#undef READ_STRING
#undef BINARY_OP
}

InterpretResult interpret(const char *source) {
    initLox(source);
    ObjFunction *function = compile(source);
    if (function == NULL) return INTERPRET_COMPILE_ERROR;

    Value functionValue;
    functionValue.type = VAL_OBJ;
    functionValue.as.obj = (Obj*)function;
    push(functionValue);

    if (setjmp(lox.errorJump) == 0) {
        ObjClosure *closure = newClosure(function);
        pop();

        Value closureValue;
        closureValue.type = VAL_OBJ;
        closureValue.as.obj = (Obj*)closure;
        push(closureValue);

        callValue(peek(0), 0);
        return run();
    } else {
        resetStack();
        return INTERPRET_RUNTIME_ERROR;
    }
}

/* --- Stage 30 architecture: callClosureFromNative --- */
/* A higher-level wrapper for natives that need to invoke a Lox
 * closure. The pattern:
 *   1. The native has already pushed the args onto the stack
 *      (via `push(value)` for each arg).
 *   2. callClosureFromNative(closure, argCount) is called.
 *   3. The function calls callClosure() to set up the new frame,
 *      then run() to execute the closure's bytecodes.
 *   4. When the closure returns (OP_RETURN pops the frame, the
 *      new frameCount is the native's caller's frame), the
 *      result is on top of the stack.
 *   5. The function pops the result and returns it.
 *
 * The "stop when we return to the caller's frame" semantic: we
 * save the caller's frameCount before calling run(). When the
 * interpreter's frameCount drops back to that depth, the closure
 * has returned and run() should exit. This is essential because
 * run() is the same interpreter loop the top-level interpret()
 * uses; without a depth check, it would continue running the
 * outer script's bytecodes from inside the native.
 *
 * On runtime error (arity mismatch, stack overflow, callee-side
 * error), runtimeError() longjmp's and the return is unreachable.
 * The native doesn't need to handle the error case; the longjmp
 * unwinds to the top-level interpret() error handler.
 *
 * This is the function natives should use, not callClosure() directly.
 * callClosure() is exposed for the lower-level use case (setting up
 * a frame without running it), which we don't currently have. */
Value callClosureFromNative(ObjClosure *closure, int argCount) {
    if (!callClosure(closure, argCount)) {
        return NIL_VAL;  /* unreachable; runtimeError longjmp'd */
    }
    /* Set the target depth to the caller's frameCount (BEFORE the
     * closure was pushed). When OP_RETURN pops the closure's frame
     * and frameCount drops to this value, run() returns. */
    vmNativeTargetDepth = vm.frameCount - 1;
    InterpretResult result = run();
    vmNativeTargetDepth = -1;  /* belt-and-suspenders; OP_RETURN also clears */
    if (result != INTERPRET_OK) {
        return NIL_VAL;  /* runtimeError longjmp'd, unreachable */
    }
    return pop();
}
