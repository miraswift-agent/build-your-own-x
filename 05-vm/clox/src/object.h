/*
 * clox — Heap objects
 *
 * Every object on the GC heap begins with an Obj header and is threaded
 * through vm.objects. String interning keeps value equality fast.
 */

#ifndef CLOX_OBJECT_H
#define CLOX_OBJECT_H

#include "chunk.h"
#include "common.h"
#include "table.h"
#include "value.h"

typedef enum {
    OBJ_STRING,
    OBJ_FUNCTION,
    OBJ_NATIVE,
    OBJ_CLOSURE,
    OBJ_UPVALUE,
    OBJ_CLASS,
    OBJ_INSTANCE,
    OBJ_BOUND_METHOD,
    OBJ_ARRAY,  /* Stage 12a: arrays are first-class heap values */
    OBJ_MODULE /* Stage 64.1: imported module (path + exports table) */
} ObjType;

struct Obj {
    ObjType type;
    bool isMarked;
    struct Obj *next;
};

typedef struct {
    Obj obj;
    int arity;
    int upvalueCount;
    Chunk chunk;
    ObjString *name;
} ObjFunction;

typedef Value (*NativeFn)(int argCount, Value *args);

typedef struct {
    Obj obj;
    NativeFn function;
} ObjNative;

struct ObjString {
    Obj obj;
    int length;
    char *chars;
    uint32_t hash;
};

typedef struct ObjUpvalue {
    Obj obj;
    Value *location;
    Value closed;
    struct ObjUpvalue *next;
} ObjUpvalue;

typedef struct {
    Obj obj;
    ObjFunction *function;
    ObjUpvalue **upvalues;
    int upvalueCount;
} ObjClosure;

typedef struct {
    Obj obj;
    ObjString *name;
    Table methods;
} ObjClass;

typedef struct {
    Obj obj;
    ObjClass *klass;
    Table fields;
} ObjInstance;

typedef struct {
    Obj obj;
    Value receiver;
    ObjClosure *method;
} ObjBoundMethod;

typedef struct {
    Obj obj;
    Value *elements;   /* malloc'd, resizable */
    int count;
    int capacity;
} ObjArray;

/* Stage 64.1 — module load state (see stage-64-modules-design.md). */
#define MODULE_LOADING 0
#define MODULE_LOADED  1

typedef struct {
    Obj obj;
    ObjString *name;   /* resolved path key (interned) */
    Table exports;     /* top-level bindings of the module */
    int state;         /* MODULE_LOADING | MODULE_LOADED */
} ObjModule;

#define OBJ_TYPE(value)     (AS_OBJ(value)->type)
#define IS_STRING(value)    isObjType(value, OBJ_STRING)
#define IS_FUNCTION(value)  isObjType(value, OBJ_FUNCTION)
#define IS_NATIVE(value)    isObjType(value, OBJ_NATIVE)
#define IS_CLOSURE(value)   isObjType(value, OBJ_CLOSURE)
#define IS_CLASS(value)     isObjType(value, OBJ_CLASS)
#define IS_INSTANCE(value)  isObjType(value, OBJ_INSTANCE)
#define IS_BOUND_METHOD(value) isObjType(value, OBJ_BOUND_METHOD)
#define IS_ARRAY(value)        isObjType(value, OBJ_ARRAY)
#define IS_MODULE(value)       isObjType(value, OBJ_MODULE)

#define AS_STRING(value)    ((ObjString*)AS_OBJ(value))
#define AS_CSTRING(value)   (((ObjString*)AS_OBJ(value))->chars)
#define AS_FUNCTION(value)  ((ObjFunction*)AS_OBJ(value))
#define AS_NATIVE(value)    (((ObjNative*)AS_OBJ(value))->function)
#define AS_CLOSURE(value)   ((ObjClosure*)AS_OBJ(value))
#define AS_CLASS(value)     ((ObjClass*)AS_OBJ(value))
#define AS_INSTANCE(value)  ((ObjInstance*)AS_OBJ(value))
#define AS_BOUND_METHOD(value) ((ObjBoundMethod*)AS_OBJ(value))
#define AS_ARRAY(value)        ((ObjArray*)AS_OBJ(value))
#define AS_MODULE(value)       ((ObjModule*)AS_OBJ(value))

static inline bool isObjType(Value value, ObjType type) {
    return IS_OBJ(value) && AS_OBJ(value)->type == type;
}

ObjString *takeString(char *chars, int length);
ObjString *copyString(const char *chars, int length);
ObjFunction *newFunction(void);
ObjNative *newNative(NativeFn function);
ObjClosure *newClosure(ObjFunction *function);
ObjUpvalue *newUpvalue(Value *slot);
ObjClass *newClass(ObjString *name);
ObjInstance *newInstance(ObjClass *klass);
ObjBoundMethod *newBoundMethod(Value receiver, ObjClosure *method);
ObjArray *newArray(int initialCapacity);
ObjModule *newModule(ObjString *name);
void arrayWrite(ObjArray *array, int index, Value value);
void arrayPush(ObjArray *array, Value value);
Value arrayRead(ObjArray *array, int index);

void printObject(Value value);
const char *objectTypeName(Obj *obj);

#endif /* CLOX_OBJECT_H */
