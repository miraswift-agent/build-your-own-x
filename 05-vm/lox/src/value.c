#include "value.h"
#include "environment.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Value nilValue(void) {
    Value v;
    v.type = VAL_NIL;
    v.as.number = 0.0;
    return v;
}

Value boolValue(bool value) {
    Value v;
    v.type = VAL_BOOL;
    v.as.boolean = value;
    return v;
}

Value numberValue(double value) {
    Value v;
    v.type = VAL_NUMBER;
    v.as.number = value;
    return v;
}

Value objValue(Obj* obj) {
    Value v;
    v.type = VAL_OBJ;
    v.as.obj = obj;
    return v;
}

Value stringValue(const char* chars, size_t length) {
    return objValue((Obj*)newString(chars, length));
}

bool isNil(Value value) { return value.type == VAL_NIL; }
bool isBool(Value value) { return value.type == VAL_BOOL; }
bool isNumber(Value value) { return value.type == VAL_NUMBER; }
bool isObjType(Value value, ObjType type) {
    return value.type == VAL_OBJ && value.as.obj->type == type;
}
bool isString(Value value) { return isObjType(value, OBJ_STRING); }
bool isFunction(Value value) { return isObjType(value, OBJ_FUNCTION); }
bool isClass(Value value) { return isObjType(value, OBJ_CLASS); }
bool isInstance(Value value) { return isObjType(value, OBJ_INSTANCE); }
bool isBoundMethod(Value value) { return isObjType(value, OBJ_BOUND_METHOD); }
bool isNative(Value value) { return isObjType(value, OBJ_NATIVE); }
bool isCallable(Value value) {
    return isFunction(value) || isClass(value) || isBoundMethod(value) || isNative(value);
}

bool asBool(Value value) { return value.as.boolean; }
double asNumber(Value value) { return value.as.number; }
ObjString* asString(Value value) { return (ObjString*)value.as.obj; }
const char* asCString(Value value) { return ((ObjString*)value.as.obj)->chars; }
ObjFunction* asFunction(Value value) { return (ObjFunction*)value.as.obj; }
ObjClass* asClass(Value value) { return (ObjClass*)value.as.obj; }
ObjInstance* asInstance(Value value) { return (ObjInstance*)value.as.obj; }
ObjBoundMethod* asBoundMethod(Value value) { return (ObjBoundMethod*)value.as.obj; }
ObjNative* asNative(Value value) { return (ObjNative*)value.as.obj; }

bool valuesEqual(Value a, Value b) {
    if (a.type != b.type) return false;
    switch (a.type) {
        case VAL_NIL:    return true;
        case VAL_BOOL:   return a.as.boolean == b.as.boolean;
        case VAL_NUMBER: return a.as.number == b.as.number;
        case VAL_OBJ: {
            ObjString* as = asString(a);
            ObjString* bs = asString(b);
            if (as->obj.type != OBJ_STRING || bs->obj.type != OBJ_STRING) {
                /* Different object identities; Lox compares by reference. */
                return a.as.obj == b.as.obj;
            }
            return as->length == bs->length &&
                   memcmp(as->chars, bs->chars, as->length) == 0;
        }
    }
    return false;
}

bool isTruthy(Value value) {
    if (isNil(value)) return false;
    if (isBool(value)) return asBool(value);
    return true;
}

const char* valueTypeName(Value value) {
    switch (value.type) {
        case VAL_NIL:    return "nil";
        case VAL_BOOL:   return "boolean";
        case VAL_NUMBER: return "number";
        case VAL_OBJ:
            switch (value.as.obj->type) {
                case OBJ_STRING: return "string";
                case OBJ_FUNCTION: return "function";
                case OBJ_CLASS: return "class";
                case OBJ_INSTANCE: return "instance";
                case OBJ_BOUND_METHOD: return "method";
                case OBJ_NATIVE: return "native";
            }
            break;
    }
    return "unknown";
}

static char* valueToString(Value value) {
    if (isNil(value)) return loxCopyString("nil", 3);
    if (isBool(value)) {
        if (asBool(value)) return loxCopyString("true", 4);
        return loxCopyString("false", 5);
    }
    if (isNumber(value)) {
        char buffer[64];
        snprintf(buffer, sizeof(buffer), "%g", asNumber(value));
        return loxCopyString(buffer, strlen(buffer));
    }
    if (isString(value)) {
        return asString(value)->chars;
    }
    if (isFunction(value)) {
        char buffer[256];
        snprintf(buffer, sizeof(buffer), "<fn %s>", asFunction(value)->name);
        return loxCopyString(buffer, strlen(buffer));
    }
    if (isClass(value)) {
        char buffer[256];
        snprintf(buffer, sizeof(buffer), "<class %s>", asClass(value)->name);
        return loxCopyString(buffer, strlen(buffer));
    }
    if (isInstance(value)) {
        char buffer[256];
        snprintf(buffer, sizeof(buffer), "<%s instance>", asInstance(value)->klass->name);
        return loxCopyString(buffer, strlen(buffer));
    }
    if (isBoundMethod(value)) {
        char buffer[256];
        snprintf(buffer, sizeof(buffer), "<bound method %s>", asBoundMethod(value)->method->name);
        return loxCopyString(buffer, strlen(buffer));
    }
    if (isNative(value)) {
        char buffer[256];
        snprintf(buffer, sizeof(buffer), "<native fn %s>", asNative(value)->name);
        return loxCopyString(buffer, strlen(buffer));
    }
    return loxCopyString("<unknown>", 9);
}

void valuePrint(Value value) {
    char* str = valueToString(value);
    printf("%s", str);
}

/* Object constructors */

ObjString* newString(const char* chars, size_t length) {
    ObjString* string = ALLOCATE(ObjString);
    string->obj.type = OBJ_STRING;
    string->length = length;
    string->chars = loxCopyString(chars, length);
    return string;
}

ObjFunction* newFunction(const char* name) {
    ObjFunction* function = ALLOCATE(ObjFunction);
    function->obj.type = OBJ_FUNCTION;
    function->name = loxCopyString(name, strlen(name));
    function->arity = 0;
    function->params = NULL;
    function->body = NULL;
    function->closure = NULL;
    return function;
}

ObjClass* newClass(const char* name, ObjClass* superclass) {
    ObjClass* klass = ALLOCATE(ObjClass);
    klass->obj.type = OBJ_CLASS;
    klass->name = loxCopyString(name, strlen(name));
    klass->superclass = superclass;
    klass->methods = NULL;
    return klass;
}

ObjInstance* newInstance(ObjClass* klass) {
    ObjInstance* instance = ALLOCATE(ObjInstance);
    instance->obj.type = OBJ_INSTANCE;
    instance->klass = klass;
    instance->fields = newEnvironment(NULL);
    return instance;
}

ObjBoundMethod* newBoundMethod(ObjInstance* receiver, ObjFunction* method) {
    ObjBoundMethod* bound = ALLOCATE(ObjBoundMethod);
    bound->obj.type = OBJ_BOUND_METHOD;
    bound->method = method;
    bound->receiver = receiver;
    return bound;
}

ObjNative* newNative(NativeFn function, int arity, const char* name) {
    ObjNative* native = ALLOCATE(ObjNative);
    native->obj.type = OBJ_NATIVE;
    native->function = function;
    native->arity = arity;
    native->name = loxCopyString(name, strlen(name));
    return native;
}
