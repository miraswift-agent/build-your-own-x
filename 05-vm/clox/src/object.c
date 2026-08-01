/*
 * clox — Heap object implementation
 */

#include <stdio.h>
#include <string.h>

#include "memory.h"
#include "object.h"
#include "table.h"
#include "value.h"
#include "vm.h"

#define ALLOCATE_OBJ(type, objectType) \
    (type*)allocateObject(sizeof(type), objectType)

static Obj *allocateObject(size_t size, ObjType type) {
    Obj *object = (Obj*)reallocate(NULL, 0, size);
    object->type = type;
    object->isMarked = false;

    object->next = vm.objects;
    vm.objects = object;

#if DEBUG_LOG_GC
    fprintf(stderr, "%p allocate %zu for %d\n", (void*)object, size, type);
#endif

    return object;
}

static uint32_t hashString(const char *key, int length) {
    uint32_t hash = 2166136261u;
    for (int i = 0; i < length; i++) {
        hash ^= (uint8_t)key[i];
        hash *= 16777619;
    }
    return hash;
}

ObjString *takeString(char *chars, int length) {
    uint32_t hash = hashString(chars, length);
    ObjString *interned = tableFindString(&vm.strings, chars, length, hash);
    if (interned != NULL) {
        FREE_ARRAY(char, chars, length + 1);
        return interned;
    }

    ObjString *string = ALLOCATE_OBJ(ObjString, OBJ_STRING);
    string->length = length;
    string->chars = chars;
    string->hash = hash;

    push(OBJ_VAL(string));
    tableSet(&vm.strings, string, NIL_VAL);
    pop();

    return string;
}

ObjString *copyString(const char *chars, int length) {
    uint32_t hash = hashString(chars, length);
    ObjString *interned = tableFindString(&vm.strings, chars, length, hash);
    if (interned != NULL) return interned;

    char *heapChars = ALLOCATE(char, length + 1);
    memcpy(heapChars, chars, length);
    heapChars[length] = '\0';

    return takeString(heapChars, length);
}

ObjFunction *newFunction(void) {
    ObjFunction *function = ALLOCATE_OBJ(ObjFunction, OBJ_FUNCTION);
    function->arity = 0;
    function->upvalueCount = 0;
    function->name = NULL;
    initChunk(&function->chunk);
    return function;
}

ObjNative *newNative(NativeFn function) {
    ObjNative *native = ALLOCATE_OBJ(ObjNative, OBJ_NATIVE);
    native->function = function;
    return native;
}

ObjClosure *newClosure(ObjFunction *function) {
    ObjUpvalue **upvalues = ALLOCATE(ObjUpvalue*, function->upvalueCount);
    for (int i = 0; i < function->upvalueCount; i++) {
        upvalues[i] = NULL;
    }

    ObjClosure *closure = ALLOCATE_OBJ(ObjClosure, OBJ_CLOSURE);
    closure->function = function;
    closure->upvalues = upvalues;
    closure->upvalueCount = function->upvalueCount;
    return closure;
}

ObjUpvalue *newUpvalue(Value *slot) {
    ObjUpvalue *upvalue = ALLOCATE_OBJ(ObjUpvalue, OBJ_UPVALUE);
    upvalue->location = slot;
    upvalue->closed = NIL_VAL;
    upvalue->next = NULL;
    return upvalue;
}

ObjClass *newClass(ObjString *name) {
    ObjClass *klass = ALLOCATE_OBJ(ObjClass, OBJ_CLASS);
    klass->name = name;
    initTable(&klass->methods);
    return klass;
}

ObjInstance *newInstance(ObjClass *klass) {
    ObjInstance *instance = ALLOCATE_OBJ(ObjInstance, OBJ_INSTANCE);
    instance->klass = klass;
    initTable(&instance->fields);
    return instance;
}

ObjBoundMethod *newBoundMethod(Value receiver, ObjClosure *method) {
    ObjBoundMethod *bound = ALLOCATE_OBJ(ObjBoundMethod, OBJ_BOUND_METHOD);
    bound->receiver = receiver;
    bound->method = method;
    return bound;
}

/* --- Stage 12a: ObjArray value type --- */

ObjArray *newArray(int initialCapacity) {
    ObjArray *array = ALLOCATE_OBJ(ObjArray, OBJ_ARRAY);
    array->count = 0;
    array->capacity = initialCapacity < 1 ? 1 : initialCapacity;
    array->elements = (Value*)malloc(sizeof(Value) * (size_t)array->capacity);
    return array;
}

void arrayWrite(ObjArray *array, int index, Value value) {
    /* Caller (the native) must bounds-check before calling. */
    array->elements[index] = value;
}

void arrayPush(ObjArray *array, Value value) {
    if (array->count + 1 > array->capacity) {
        int oldCapacity = array->capacity;
        array->capacity = GROW_CAPACITY(oldCapacity);
        array->elements = (Value*)realloc(array->elements,
                                          sizeof(Value) * (size_t)array->capacity);
    }
    array->elements[array->count++] = value;
}

Value arrayRead(ObjArray *array, int index) {
    /* Caller must bounds-check. */
    return array->elements[index];
}

/* --- Stage 64.1: ObjModule --- */

ObjModule *newModule(ObjString *name) {
    ObjModule *module = ALLOCATE_OBJ(ObjModule, OBJ_MODULE);
    module->name = name;
    module->state = MODULE_LOADING;
    initTable(&module->exports);
    return module;
}

void printObject(Value value) {
    switch (OBJ_TYPE(value)) {
        case OBJ_STRING:
            printf("%s", AS_CSTRING(value));
            break;
        case OBJ_FUNCTION:
            if (AS_FUNCTION(value)->name == NULL) {
                printf("<script>");
            } else {
                printf("<fn %s>", AS_FUNCTION(value)->name->chars);
            }
            break;
        case OBJ_NATIVE:
            printf("<native fn>");
            break;
        case OBJ_CLOSURE:
            if (AS_CLOSURE(value)->function->name == NULL) {
                printf("<script>");
            } else {
                printf("<fn %s>", AS_CLOSURE(value)->function->name->chars);
            }
            break;
        case OBJ_UPVALUE:
            printf("upvalue");
            break;
        case OBJ_CLASS:
            printf("%s", AS_CLASS(value)->name->chars);
            break;
        case OBJ_INSTANCE:
            printf("%s instance", AS_INSTANCE(value)->klass->name->chars);
            break;
        case OBJ_BOUND_METHOD:
            printf("<bound %s>",
                   AS_BOUND_METHOD(value)->method->function->name->chars);
            break;
        case OBJ_ARRAY:
            printf("[");
            for (int i = 0; i < AS_ARRAY(value)->count; i++) {
                if (i > 0) printf(", ");
                printValue(AS_ARRAY(value)->elements[i]);
            }
            printf("]");
            break;
        case OBJ_MODULE:
            printf("<module %s>", AS_MODULE(value)->name->chars);
            break;
    }
}

const char *objectTypeName(Obj *obj) {
    switch (obj->type) {
        case OBJ_STRING:       return "string";
        case OBJ_FUNCTION:     return "function";
        case OBJ_NATIVE:       return "native";
        case OBJ_CLOSURE:      return "closure";
        case OBJ_UPVALUE:      return "upvalue";
        case OBJ_CLASS:        return "class";
        case OBJ_INSTANCE:     return "instance";
        case OBJ_BOUND_METHOD: return "bound method";
        case OBJ_ARRAY:        return "array";
        case OBJ_MODULE:       return "module";
    }
    return "object";
}
