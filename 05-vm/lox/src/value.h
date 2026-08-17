#ifndef LOX_VALUE_H
#define LOX_VALUE_H

#include "lox.h"
#include "token.h"

/* Forward declarations */
typedef struct Environment Environment;

typedef enum {
    VAL_NIL,
    VAL_BOOL,
    VAL_NUMBER,
    VAL_OBJ
} ValueType;

typedef enum {
    OBJ_STRING,
    OBJ_FUNCTION,
    OBJ_CLASS,
    OBJ_INSTANCE,
    OBJ_BOUND_METHOD,
    OBJ_NATIVE
} ObjType;

typedef struct Obj {
    ObjType type;
} Obj;

typedef struct {
    Obj obj;
    size_t length;
    char* chars;
} ObjString;

typedef struct {
    Obj obj;
    char* name;
    int arity;
    Token* params;     /* parameter name tokens (arity of them) */
    struct Stmt* body; /* block statement */
    Environment* closure;
} ObjFunction;

typedef struct ObjClass {
    Obj obj;
    char* name;
    struct ObjClass* superclass;
    Environment* methods;  /* name -> ObjFunction* */
} ObjClass;

typedef struct {
    Obj obj;
    ObjClass* klass;
    Environment* fields;   /* name -> Value */
} ObjInstance;

typedef struct {
    Obj obj;
    ObjFunction* method;
    ObjInstance* receiver;
} ObjBoundMethod;

/* Value representation */
typedef struct {
    ValueType type;
    union {
        bool boolean;
        double number;
        Obj* obj;
    } as;
} Value;

/* Native function handle — must appear after Value is defined. */
typedef Value (*NativeFn)(int argCount, Value* args);

typedef struct {
    Obj obj;
    NativeFn function;
    int arity;
    char* name;
} ObjNative;

/* Value constructors */
Value nilValue(void);
Value boolValue(bool value);
Value numberValue(double value);
Value objValue(Obj* obj);
Value stringValue(const char* chars, size_t length);

/* Type predicates */
bool isNil(Value value);
bool isBool(Value value);
bool isNumber(Value value);
bool isString(Value value);
bool isFunction(Value value);
bool isClass(Value value);
bool isInstance(Value value);
bool isBoundMethod(Value value);
bool isNative(Value value);
bool isCallable(Value value);

/* Accessors */
bool asBool(Value value);
double asNumber(Value value);
ObjString* asString(Value value);
const char* asCString(Value value);
ObjFunction* asFunction(Value value);
ObjClass* asClass(Value value);
ObjInstance* asInstance(Value value);
ObjBoundMethod* asBoundMethod(Value value);
ObjNative* asNative(Value value);

/* Helpers */
bool valuesEqual(Value a, Value b);
bool isTruthy(Value value);
void valuePrint(Value value);
const char* valueTypeName(Value value);

/* Object constructors */
ObjString* newString(const char* chars, size_t length);
ObjFunction* newFunction(const char* name);
ObjClass* newClass(const char* name, ObjClass* superclass);
ObjInstance* newInstance(ObjClass* klass);
ObjBoundMethod* newBoundMethod(ObjInstance* receiver, ObjFunction* method);
ObjNative* newNative(NativeFn function, int arity, const char* name);

#endif /* LOX_VALUE_H */
