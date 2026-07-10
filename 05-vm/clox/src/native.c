/*
 * clox — Native function implementation
 *
 * Each native is a C function that takes (argCount, args) where `args`
 * points to the first argument on the VM stack. The native returns a
 * Value; the VM machinery in vm.c::callValue() pops the args and pushes
 * the result. Native functions must:
 *   - Validate argCount and reject wrong arity with runtimeError().
 *   - Validate arg types and reject wrong types with runtimeError().
 *   - Allocate any heap-allocated results (strings, etc.) through
 *     copyString() / takeString() so the GC sees them.
 *   - Use the macros from value.h / object.h to inspect and construct
 *     Values, never poke at the Value union directly.
 *
 * Stage 7 added: number_abs, number_min, number_max, string_length,
 * string_upper, string_lower, typeof. The existing clock() is left
 * unchanged.
 *
 * Naming convention: flat namespace with category prefix (number_*, string_*,
 * typeof). See stage-7 close-out doc for the design rationale (rejected:
 * class-as-namespace, because OP_GET_PROPERTY only works on instances and
 * adding static methods would change the language semantics).
 */

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "native.h"
#include "lox.h"
#include "memory.h"
#include "object.h"
#include "vm.h"

static Value clockNative(int argCount, Value *args) {
    (void)argCount;
    (void)args;
    return NUMBER_VAL((double)clock() / CLOCKS_PER_SEC);
}

static Value numberAbsNative(int argCount, Value *args) {
    if (argCount != 1) {
        runtimeError("number_abs() takes 1 argument (%d given).", argCount);
        return NIL_VAL;  /* unreachable; runtimeError does longjmp */
    }
    if (!IS_NUMBER(args[0])) {
        runtimeError("number_abs() argument must be a number.");
        return NIL_VAL;
    }
    double x = AS_NUMBER(args[0]);
    return NUMBER_VAL(x < 0 ? -x : x);
}

static Value numberMinNative(int argCount, Value *args) {
    if (argCount != 2) {
        runtimeError("number_min() takes 2 arguments (%d given).", argCount);
        return NIL_VAL;
    }
    if (!IS_NUMBER(args[0]) || !IS_NUMBER(args[1])) {
        runtimeError("number_min() arguments must be numbers.");
        return NIL_VAL;
    }
    double a = AS_NUMBER(args[0]);
    double b = AS_NUMBER(args[1]);
    return NUMBER_VAL(a < b ? a : b);
}

static Value numberMaxNative(int argCount, Value *args) {
    if (argCount != 2) {
        runtimeError("number_max() takes 2 arguments (%d given).", argCount);
        return NIL_VAL;
    }
    if (!IS_NUMBER(args[0]) || !IS_NUMBER(args[1])) {
        runtimeError("number_max() arguments must be numbers.");
        return NIL_VAL;
    }
    double a = AS_NUMBER(args[0]);
    double b = AS_NUMBER(args[1]);
    return NUMBER_VAL(a > b ? a : b);
}

static Value stringLengthNative(int argCount, Value *args) {
    if (argCount != 1) {
        runtimeError("string_length() takes 1 argument (%d given).", argCount);
        return NIL_VAL;
    }
    if (!IS_STRING(args[0])) {
        runtimeError("string_length() argument must be a string.");
        return NIL_VAL;
    }
    ObjString *s = AS_STRING(args[0]);
    return NUMBER_VAL((double)s->length);
}

static Value stringUpperNative(int argCount, Value *args) {
    if (argCount != 1) {
        runtimeError("string_upper() takes 1 argument (%d given).", argCount);
        return NIL_VAL;
    }
    if (!IS_STRING(args[0])) {
        runtimeError("string_upper() argument must be a string.");
        return NIL_VAL;
    }
    ObjString *s = AS_STRING(args[0]);
    /* Allocate a fresh buffer for the result. copyString() will register
     * it with the GC and intern it (so a second call with the same input
     * returns the same ObjString pointer, which is a small efficiency
     * win for repeated calls in a loop). */
    char *upper = ALLOCATE(char, s->length + 1);
    for (int i = 0; i < s->length; i++) {
        upper[i] = (char)toupper((unsigned char)s->chars[i]);
    }
    upper[s->length] = '\0';
    ObjString *result = copyString(upper, s->length);
    /* ALLOCATE() memory is owned by us; free it now that copyString has
     * copied the contents. */
    FREE_ARRAY(char, upper, s->length + 1);
    return OBJ_VAL(result);
}

static Value stringLowerNative(int argCount, Value *args) {
    if (argCount != 1) {
        runtimeError("string_lower() takes 1 argument (%d given).", argCount);
        return NIL_VAL;
    }
    if (!IS_STRING(args[0])) {
        runtimeError("string_lower() argument must be a string.");
        return NIL_VAL;
    }
    ObjString *s = AS_STRING(args[0]);
    char *lower = ALLOCATE(char, s->length + 1);
    for (int i = 0; i < s->length; i++) {
        lower[i] = (char)tolower((unsigned char)s->chars[i]);
    }
    lower[s->length] = '\0';
    ObjString *result = copyString(lower, s->length);
    FREE_ARRAY(char, lower, s->length + 1);
    return OBJ_VAL(result);
}

static Value stringSubstringNative(int argCount, Value *args) {
    if (argCount != 3) {
        runtimeError("string_substring() takes 3 arguments (%d given).", argCount);
        return NIL_VAL;
    }
    if (!IS_STRING(args[0])) {
        runtimeError("string_substring() argument 0 must be a string.");
        return NIL_VAL;
    }
    if (!IS_NUMBER(args[1]) || !IS_NUMBER(args[2])) {
        runtimeError("string_substring() arguments 1 and 2 must be numbers.");
        return NIL_VAL;
    }
    ObjString *s = AS_STRING(args[0]);
    double startD = AS_NUMBER(args[1]);
    double endD   = AS_NUMBER(args[2]);
    /* Clamp. start below 0 → 0. end above length → length. start > end → empty. */
    int start = (int)startD;
    int end   = (int)endD;
    if (start < 0) start = 0;
    if (end > s->length) end = s->length;
    if (start > end) start = end;
    /* copyString() copies the bytes, so the substring is a fresh ObjString. */
    ObjString *result = copyString(s->chars + start, (end - start));
    return OBJ_VAL(result);
}

static Value stringContainsNative(int argCount, Value *args) {
    if (argCount != 2) {
        runtimeError("string_contains() takes 2 arguments (%d given).", argCount);
        return NIL_VAL;
    }
    if (!IS_STRING(args[0]) || !IS_STRING(args[1])) {
        runtimeError("string_contains() arguments must be strings.");
        return NIL_VAL;
    }
    ObjString *haystack = AS_STRING(args[0]);
    ObjString *needle   = AS_STRING(args[1]);
    /* Empty needle is contained in any string. */
    if (needle->length == 0) return BOOL_VAL(true);
    if (needle->length > haystack->length) return BOOL_VAL(false);
    /* Naive substring search. KMP or Boyer-Moore is overkill for a
     * stdlib primitive on a 1k-10k string; the Lox use case is small. */
    for (int i = 0; i <= haystack->length - needle->length; i++) {
        bool match = true;
        for (int j = 0; j < needle->length; j++) {
            if (haystack->chars[i + j] != needle->chars[j]) {
                match = false;
                break;
            }
        }
        if (match) return BOOL_VAL(true);
    }
    return BOOL_VAL(false);
}

static Value stringReplaceNative(int argCount, Value *args) {
    if (argCount != 3) {
        runtimeError("string_replace() takes 3 arguments (%d given).", argCount);
        return NIL_VAL;
    }
    if (!IS_STRING(args[0]) || !IS_STRING(args[1]) || !IS_STRING(args[2])) {
        runtimeError("string_replace() arguments must be strings.");
        return NIL_VAL;
    }
    ObjString *s      = AS_STRING(args[0]);
    ObjString *oldStr = AS_STRING(args[1]);
    ObjString *newStr = AS_STRING(args[2]);
    /* Find the first occurrence of oldStr in s. */
    int found = -1;
    if (oldStr->length > 0 && oldStr->length <= s->length) {
        for (int i = 0; i <= s->length - oldStr->length; i++) {
            bool match = true;
            for (int j = 0; j < oldStr->length; j++) {
                if (s->chars[i + j] != oldStr->chars[j]) {
                    match = false;
                    break;
                }
            }
            if (match) { found = i; break; }
        }
    }
    if (found < 0) {
        /* Not found: return a fresh copy of s (so the caller can rely
         * on getting an ObjString either way). copyString() interns the
         * result, so this is essentially a no-op cost. */
        ObjString *result = copyString(s->chars, s->length);
        return OBJ_VAL(result);
    }
    /* Build the new string: s[0..found] + newStr + s[found+old..s.length]. */
    int newLen = s->length - oldStr->length + newStr->length;
    char *buf = ALLOCATE(char, newLen + 1);
    memcpy(buf, s->chars, found);
    memcpy(buf + found, newStr->chars, newStr->length);
    memcpy(buf + found + newStr->length, s->chars + found + oldStr->length,
           s->length - found - oldStr->length);
    buf[newLen] = '\0';
    ObjString *result = copyString(buf, newLen);
    FREE_ARRAY(char, buf, newLen + 1);
    return OBJ_VAL(result);
}

static bool startsWithString(ObjString *s, ObjString *prefix) {
    if (prefix->length > s->length) return false;
    return memcmp(s->chars, prefix->chars, (size_t)prefix->length) == 0;
}

static Value stringStartsWithNative(int argCount, Value *args) {
    if (argCount != 2) {
        runtimeError("string_starts_with() takes 2 arguments (%d given).", argCount);
        return NIL_VAL;
    }
    if (!IS_STRING(args[0]) || !IS_STRING(args[1])) {
        runtimeError("string_starts_with() arguments must be strings.");
        return NIL_VAL;
    }
    return BOOL_VAL(startsWithString(AS_STRING(args[0]), AS_STRING(args[1])));
}

static bool endsWithString(ObjString *s, ObjString *suffix) {
    if (suffix->length > s->length) return false;
    int offset = s->length - suffix->length;
    return memcmp(s->chars + offset, suffix->chars, (size_t)suffix->length) == 0;
}

static Value stringEndsWithNative(int argCount, Value *args) {
    if (argCount != 2) {
        runtimeError("string_ends_with() takes 2 arguments (%d given).", argCount);
        return NIL_VAL;
    }
    if (!IS_STRING(args[0]) || !IS_STRING(args[1])) {
        runtimeError("string_ends_with() arguments must be strings.");
        return NIL_VAL;
    }
    return BOOL_VAL(endsWithString(AS_STRING(args[0]), AS_STRING(args[1])));
}

static Value stringIndexOfNative(int argCount, Value *args) {
    if (argCount != 2) {
        runtimeError("string_index_of() takes 2 arguments (%d given).", argCount);
        return NIL_VAL;
    }
    if (!IS_STRING(args[0]) || !IS_STRING(args[1])) {
        runtimeError("string_index_of() arguments must be strings.");
        return NIL_VAL;
    }
    ObjString *haystack = AS_STRING(args[0]);
    ObjString *needle   = AS_STRING(args[1]);
    if (needle->length == 0) return NUMBER_VAL(0);
    if (needle->length > haystack->length) return NUMBER_VAL(-1);
    /* Reuse the same naive search as string_contains, but return the
     * position of the first match (or -1 if not found). */
    for (int i = 0; i <= haystack->length - needle->length; i++) {
        bool match = true;
        for (int j = 0; j < needle->length; j++) {
            if (haystack->chars[i + j] != needle->chars[j]) {
                match = false;
                break;
            }
        }
        if (match) return NUMBER_VAL((double)i);
    }
    return NUMBER_VAL(-1);
}

static bool isWhitespace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

static Value stringTrimNative(int argCount, Value *args) {
    if (argCount != 1) {
        runtimeError("string_trim() takes 1 argument (%d given).", argCount);
        return NIL_VAL;
    }
    if (!IS_STRING(args[0])) {
        runtimeError("string_trim() argument must be a string.");
        return NIL_VAL;
    }
    ObjString *s = AS_STRING(args[0]);
    int start = 0;
    int end = s->length;
    while (start < end && isWhitespace(s->chars[start])) start++;
    while (end > start && isWhitespace(s->chars[end - 1])) end--;
    if (start == end) {
        /* All whitespace: return a fresh empty string. */
        ObjString *result = copyString("", 0);
        return OBJ_VAL(result);
    }
    ObjString *result = copyString(s->chars + start, end - start);
    return OBJ_VAL(result);
}

static Value numberFloorNative(int argCount, Value *args) {
    if (argCount != 1) {
        runtimeError("number_floor() takes 1 argument (%d given).", argCount);
        return NIL_VAL;
    }
    if (!IS_NUMBER(args[0])) {
        runtimeError("number_floor() argument must be a number.");
        return NIL_VAL;
    }
    return NUMBER_VAL(floor(AS_NUMBER(args[0])));
}

static Value numberCeilNative(int argCount, Value *args) {
    if (argCount != 1) {
        runtimeError("number_ceil() takes 1 argument (%d given).", argCount);
        return NIL_VAL;
    }
    if (!IS_NUMBER(args[0])) {
        runtimeError("number_ceil() argument must be a number.");
        return NIL_VAL;
    }
    return NUMBER_VAL(ceil(AS_NUMBER(args[0])));
}

static Value numberRoundNative(int argCount, Value *args) {
    if (argCount != 1) {
        runtimeError("number_round() takes 1 argument (%d given).", argCount);
        return NIL_VAL;
    }
    if (!IS_NUMBER(args[0])) {
        runtimeError("number_round() argument must be a number.");
        return NIL_VAL;
    }
    /* C's round() rounds half away from zero, not banker's rounding.
     * Documented in the close-out doc. */
    return NUMBER_VAL(round(AS_NUMBER(args[0])));
}

static Value numberSqrtNative(int argCount, Value *args) {
    if (argCount != 1) {
        runtimeError("number_sqrt() takes 1 argument (%d given).", argCount);
        return NIL_VAL;
    }
    if (!IS_NUMBER(args[0])) {
        runtimeError("number_sqrt() argument must be a number.");
        return NIL_VAL;
    }
    double x = AS_NUMBER(args[0]);
    if (x < 0) {
        /* Negative input: print a runtime error and return NaN. sqrt(-1)
         * is mathematically undefined; the Lox-level error message lets
         * the user know what they did wrong. */
        runtimeError("number_sqrt() argument must be non-negative.");
        return NUMBER_VAL(0.0 / 0.0);  /* NaN; unreachable */
    }
    return NUMBER_VAL(sqrt(x));
}

static Value numberPowNative(int argCount, Value *args) {
    if (argCount != 2) {
        runtimeError("number_pow() takes 2 arguments (%d given).", argCount);
        return NIL_VAL;
    }
    if (!IS_NUMBER(args[0]) || !IS_NUMBER(args[1])) {
        runtimeError("number_pow() arguments must be numbers.");
        return NIL_VAL;
    }
    return NUMBER_VAL(pow(AS_NUMBER(args[0]), AS_NUMBER(args[1])));
}

static Value typeofNative(int argCount, Value *args) {
    if (argCount != 1) {
        runtimeError("typeof() takes 1 argument (%d given).", argCount);
        return NIL_VAL;
    }
    /* Inspect the Value's type tag directly. Every value type in clox is
     * represented in this switch — adding a new value type to clox
     * requires extending this switch too. That's the cost of a native
     * type predicate; the alternative is a VM opcode, but a native
     * keeps the VM unchanged. */
    const char *name;
    if (IS_BOOL(args[0]))        name = "bool";
    else if (IS_NIL(args[0]))    name = "nil";
    else if (IS_NUMBER(args[0])) name = "number";
    else if (IS_OBJ(args[0])) {
        switch (OBJ_TYPE(args[0])) {
            case OBJ_STRING:       name = "string";  break;
            case OBJ_NATIVE:       name = "function"; break;
            case OBJ_CLOSURE:      name = "function"; break;
            case OBJ_FUNCTION:     name = "function"; break;
            case OBJ_CLASS:        name = "class";    break;
            case OBJ_INSTANCE:     name = "instance"; break;
            case OBJ_BOUND_METHOD: name = "method";   break;
            case OBJ_UPVALUE:      name = "upvalue";  break;
            default:               name = "object";   break;
        }
    } else {
        name = "unknown";
    }
    return OBJ_VAL(copyString(name, (int)strlen(name)));
}

void defineNatives(void) {
    /* Existing from stage 05. */
    ObjString *name = copyString("clock", (int)strlen("clock"));
    push(OBJ_VAL(name));
    tableSet(&vm.globals, name, OBJ_VAL(newNative(clockNative)));
    pop();

    /* Stage 7: number operations. */
    name = copyString("number_abs", (int)strlen("number_abs"));
    push(OBJ_VAL(name));
    tableSet(&vm.globals, name, OBJ_VAL(newNative(numberAbsNative)));
    pop();

    name = copyString("number_min", (int)strlen("number_min"));
    push(OBJ_VAL(name));
    tableSet(&vm.globals, name, OBJ_VAL(newNative(numberMinNative)));
    pop();

    name = copyString("number_max", (int)strlen("number_max"));
    push(OBJ_VAL(name));
    tableSet(&vm.globals, name, OBJ_VAL(newNative(numberMaxNative)));
    pop();

    /* Stage 7: string operations. */
    name = copyString("string_length", (int)strlen("string_length"));
    push(OBJ_VAL(name));
    tableSet(&vm.globals, name, OBJ_VAL(newNative(stringLengthNative)));
    pop();

    name = copyString("string_upper", (int)strlen("string_upper"));
    push(OBJ_VAL(name));
    tableSet(&vm.globals, name, OBJ_VAL(newNative(stringUpperNative)));
    pop();

    name = copyString("string_lower", (int)strlen("string_lower"));
    push(OBJ_VAL(name));
    tableSet(&vm.globals, name, OBJ_VAL(newNative(stringLowerNative)));
    pop();

    /* Stage 8: more string operations. */
    name = copyString("string_substring", (int)strlen("string_substring"));
    push(OBJ_VAL(name));
    tableSet(&vm.globals, name, OBJ_VAL(newNative(stringSubstringNative)));
    pop();

    name = copyString("string_contains", (int)strlen("string_contains"));
    push(OBJ_VAL(name));
    tableSet(&vm.globals, name, OBJ_VAL(newNative(stringContainsNative)));
    pop();

    name = copyString("string_replace", (int)strlen("string_replace"));
    push(OBJ_VAL(name));
    tableSet(&vm.globals, name, OBJ_VAL(newNative(stringReplaceNative)));
    pop();

    /* Stage 9: even more string operations. */
    name = copyString("string_starts_with", (int)strlen("string_starts_with"));
    push(OBJ_VAL(name));
    tableSet(&vm.globals, name, OBJ_VAL(newNative(stringStartsWithNative)));
    pop();

    name = copyString("string_ends_with", (int)strlen("string_ends_with"));
    push(OBJ_VAL(name));
    tableSet(&vm.globals, name, OBJ_VAL(newNative(stringEndsWithNative)));
    pop();

    name = copyString("string_index_of", (int)strlen("string_index_of"));
    push(OBJ_VAL(name));
    tableSet(&vm.globals, name, OBJ_VAL(newNative(stringIndexOfNative)));
    pop();

    name = copyString("string_trim", (int)strlen("string_trim"));
    push(OBJ_VAL(name));
    tableSet(&vm.globals, name, OBJ_VAL(newNative(stringTrimNative)));
    pop();

    /* Stage 10: more number operations. */
    name = copyString("number_floor", (int)strlen("number_floor"));
    push(OBJ_VAL(name));
    tableSet(&vm.globals, name, OBJ_VAL(newNative(numberFloorNative)));
    pop();

    name = copyString("number_ceil", (int)strlen("number_ceil"));
    push(OBJ_VAL(name));
    tableSet(&vm.globals, name, OBJ_VAL(newNative(numberCeilNative)));
    pop();

    name = copyString("number_round", (int)strlen("number_round"));
    push(OBJ_VAL(name));
    tableSet(&vm.globals, name, OBJ_VAL(newNative(numberRoundNative)));
    pop();

    name = copyString("number_sqrt", (int)strlen("number_sqrt"));
    push(OBJ_VAL(name));
    tableSet(&vm.globals, name, OBJ_VAL(newNative(numberSqrtNative)));
    pop();

    name = copyString("number_pow", (int)strlen("number_pow"));
    push(OBJ_VAL(name));
    tableSet(&vm.globals, name, OBJ_VAL(newNative(numberPowNative)));
    pop();

    /* Stage 7: type predicate. */
    name = copyString("typeof", (int)strlen("typeof"));
    push(OBJ_VAL(name));
    tableSet(&vm.globals, name, OBJ_VAL(newNative(typeofNative)));
    pop();
}
