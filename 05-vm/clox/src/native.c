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
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
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

/* string(n) -> string. Convert a number to its string representation.
 *
 * The "round-trip" format: "%.14g" produces the shortest string that
 * parses back to the same double. So 3.14 -> "3.14" (not "3.140000"
 * or "3.1399999999999999"), 5.0 -> "5" (not "5.0"), and integers
 * stay integers. 42 -> "42", -7 -> "-7", 0 -> "0".
 *
 * Why this matters: clox's + operator requires matching types
 * (string+string or number+number), and there is no implicit
 * number-to-string conversion. Before this native, the only way
 * to print a number in a sentence was a separate `print(n)` call.
 * Now you can do `print "I am " + string(age) + " years old"`. */
static Value stringFromNumberNative(int argCount, Value *args) {
    if (argCount != 1) {
        runtimeError("string() takes 1 argument (%d given).", argCount);
        return NIL_VAL;
    }
    if (!IS_NUMBER(args[0])) {
        runtimeError("string() argument must be a number.");
        return NIL_VAL;
    }
    /* snprintf with %.14g gives the shortest round-trip
     * representation for double-precision floats. The 32-byte
     * buffer is enough for any double: the longest %g output
     * for a double is 24 characters (sign, 17 digits, decimal
     * point, e+xxx with sign), well within 32. */
    char buf[32];
    int n = snprintf(buf, sizeof(buf), "%.14g", AS_NUMBER(args[0]));
    if (n < 0) return NIL_VAL;
    /* copyString will intern the result so repeated calls with
     * the same input return the same ObjString pointer. */
    return OBJ_VAL(copyString(buf, n));
}

/* --- Stage 24: string_to_number --- */
/* string_to_number(s) -> number. The inverse of Stage 17's string(n).
 * Parses a decimal number from a string. JavaScript's parseFloat
 * semantics with strict-error-on-failure: leading/trailing whitespace
 * is skipped, an optional sign, then a non-empty sequence of digits
 * with at most one decimal point, an optional exponent. Errors:
 * empty input, no characters consumed (e.g. "abc"), and overflow
 * to +/-Infinity. Single allocation: none. The function uses strtod
 * to do the actual parsing (it's a libc-provided inverse of
 * snprintf("%.14g", n), and it handles all the edge cases the
 * string(n) impl cares about). NaN is never returned — we error
 * on any condition strtod considers "no number parsed" or "out
 * of range."
 *
 * --- Stage 25: string_to_number(s, base) --- */
/* Extended with an optional base argument. 1-arg form (Stage 24)
 * uses strtod (decimal + exponent). 2-arg form (Stage 25) uses
 * strtol with the given base. base in [2, 36] or 0 (auto-detect
 * from prefix: '0x' -> hex, '0' -> octal, else decimal — C strtol
 * / Python int() convention). Same strict-error contract: empty
 * input, no chars consumed, base out of [2, 36] and not 0, and
 * overflow to LONG_MIN / LONG_MAX all error.
 *
 * Both forms share the same error message text and the same
 * "no NaN, no Infinity" guarantee. The 2-arg form's strtol path
 * does not support fractional input ("3.14" with base 10 errors,
 * because strtol stops at the '.') — this is the C / Python
 * integer-parse convention. For decimal floats, use the 1-arg
 * form. No allocation: strtod/strtol return primitives directly. */
static Value stringToNumberNative(int argCount, Value *args) {
    if (argCount != 1 && argCount != 2) {
        runtimeError("string_to_number() takes 1 or 2 arguments (%d given).", argCount);
        return NIL_VAL;
    }
    if (!IS_STRING(args[0])) {
        runtimeError("string_to_number() argument 0 must be a string.");
        return NIL_VAL;
    }
    ObjString *s = AS_STRING(args[0]);
    if (argCount == 1) {
        /* 1-arg form: strtod, decimal + exponent, JS parseFloat
         * semantics. */
        char *endptr;
        errno = 0;
        double result = strtod(s->chars, &endptr);
        if (endptr == s->chars) {
            runtimeError("string_to_number() could not parse a number from the input.");
            return NIL_VAL;
        }
        if (errno == ERANGE) {
            runtimeError("string_to_number() input is out of range (overflow).");
            return NIL_VAL;
        }
        return NUMBER_VAL(result);
    } else {
        /* 2-arg form: strtol with base. base must be 0 (auto-detect)
         * or in [2, 36]. */
        if (!IS_NUMBER(args[1])) {
            runtimeError("string_to_number() argument 1 must be a number.");
            return NIL_VAL;
        }
        double baseD = AS_NUMBER(args[1]);
        if (baseD != (int)baseD) {
            runtimeError("string_to_number() argument 1 must be an integer.");
            return NIL_VAL;
        }
        int base = (int)baseD;
        if (base != 0 && (base < 2 || base > 36)) {
            runtimeError("string_to_number() base must be 0 or in [2, 36] (got %d).", base);
            return NIL_VAL;
        }
        char *endptr;
        errno = 0;
        long result = strtol(s->chars, &endptr, base);
        if (endptr == s->chars) {
            runtimeError("string_to_number() could not parse a number from the input.");
            return NIL_VAL;
        }
        if (errno == ERANGE) {
            runtimeError("string_to_number() input is out of range (overflow).");
            return NIL_VAL;
        }
        return NUMBER_VAL((double)result);
    }
}

/* --- Stage 26: string_to_int --- */
/* string_to_int(s) -> number. A NEW native (not an extension of
 * string_to_number) — distinct conceptual purpose: integer-only
 * parse. Always uses strtol with base 10 (no float path, no base
 * parameter). Closes the '42 vs 42.0' question: string_to_int("42.5")
 * errors (strtol stops at the '.', so endptr == startptr is false,
 * but mid-string stop is treated as "no integer parsed"), and
 * string_to_int("42") returns 42.0 (a double, since clox's number
 * type is double — the user's intent is integer, the type is still
 * double).
 *
 * Same strict-error contract as Stage 24/25: empty input, no chars
 * consumed, and overflow to LONG_MIN / LONG_MAX all error. No NaN,
 * no Infinity. The function does NOT trim — if the user wants
 * trim-then-parse, they compose string_trim (Stage 9) with
 * string_to_int.
 *
 * Design decisions: (1) "42.5" errors (strtol stops at the '.',
 * we treat mid-string stop as "no integer parsed"). This matches
 * Python's int("42.5") error shape, not JavaScript's parseInt
 * ("42.5") === 42 silent-truncate. The discipline: parser natives
 * are loud on garbage, not lenient. (2) Always base 10 — no
 * auto-detect, no base parameter. If the user wants base 16
 * parsing, they use string_to_number(s, 16). The two natives
 * have distinct conceptual purposes. (3) No allocation: strtol
 * returns a long directly; NUMBER_VAL is a tagged-union wrap. */
static Value stringToIntNative(int argCount, Value *args) {
    if (argCount != 1) {
        runtimeError("string_to_int() takes 1 argument (%d given).", argCount);
        return NIL_VAL;
    }
    if (!IS_STRING(args[0])) {
        runtimeError("string_to_int() argument must be a string.");
        return NIL_VAL;
    }
    ObjString *s = AS_STRING(args[0]);
    /* strtol with base 10. The strictness rule: all characters
     * must be consumed. strtol's default behavior is "consume as
     * much as possible, ignore the rest" — e.g. strtol("42abc")
     * returns 42 with endptr pointing to "abc". We override that
     * to error on any non-trailing-EOF: string_to_int("42abc")
     * errors, not silently returns 42. This matches Python's
     * int("42abc") error shape, not JavaScript's parseInt("42abc")
     * === 42 silent-truncate.
     *
     * The check is: endptr must point to the trailing null byte
     * (i.e., all of s->chars was consumed). If endptr points
     * mid-string (e.g. "42.5", "42abc", "  42"), error. This
     * catches:
     *   - "42.5"   endptr at "."
     *   - "42abc"  endptr at "abc"
     *   - "  42"   endptr at "  42" (strtol skips leading WS by
     *              default; we want strict, no leading WS)
     *   - "3.14"   endptr at "."
     *   - ""       endptr at s->chars
     *   - "abc"    endptr at s->chars
     *
     * Note: we do NOT allow leading '+' for negative numbers
     * (strtol accepts "+42" as 42; we follow the C convention
     * but it's a minor difference from the strict integer
     * parser). If a stricter check is needed in a future stage,
     * a custom hand-rolled parser is the way (no leading '+', no
     * leading WS). */
    char *endptr;
    errno = 0;
    long result = strtol(s->chars, &endptr, 10);
    if (endptr == s->chars || endptr != s->chars + s->length) {
        /* Either no characters consumed (empty, "abc", "  ") or
         * some but not all characters consumed ("42.5", "42abc",
         * "  42"). Both error the same way: "could not parse an
         * integer from the input." */
        runtimeError("string_to_int() could not parse an integer from the input.");
        return NIL_VAL;
    }
    if (errno == ERANGE) {
        runtimeError("string_to_int() input is out of range (overflow).");
        return NIL_VAL;
    }
    return NUMBER_VAL((double)result);
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

/* --- Stage 27: string_trim_start / string_trim_end --- */
/* Two new natives, mirror of Stage 9's string_trim. trimStart
 * removes leading whitespace only (leaves trailing intact);
 * trimEnd removes trailing whitespace only (leaves leading
 * intact). JS reference: String.prototype.trimStart / trimEnd
 * (also exposed as trimLeft / trimRight in older specs). The
 * implementation is the same shape as stringTrimNative but with
 * only one of the two while-loops. isWhitespace() handles spaces,
 * tabs, and newlines (the same set stringTrimNative uses). All-
 * whitespace input returns a fresh empty string (the same all-ws
 * case stringTrimNative handles). No allocation beyond the
 * copyString call (single allocation per call). */
static Value stringTrimStartNative(int argCount, Value *args) {
    if (argCount != 1) {
        runtimeError("string_trim_start() takes 1 argument (%d given).", argCount);
        return NIL_VAL;
    }
    if (!IS_STRING(args[0])) {
        runtimeError("string_trim_start() argument must be a string.");
        return NIL_VAL;
    }
    ObjString *s = AS_STRING(args[0]);
    int start = 0;
    int end = s->length;
    while (start < end && isWhitespace(s->chars[start])) start++;
    if (start == end) {
        /* All whitespace: return a fresh empty string. */
        ObjString *result = copyString("", 0);
        return OBJ_VAL(result);
    }
    ObjString *result = copyString(s->chars + start, end - start);
    return OBJ_VAL(result);
}

static Value stringTrimEndNative(int argCount, Value *args) {
    if (argCount != 1) {
        runtimeError("string_trim_end() takes 1 argument (%d given).", argCount);
        return NIL_VAL;
    }
    if (!IS_STRING(args[0])) {
        runtimeError("string_trim_end() argument must be a string.");
        return NIL_VAL;
    }
    ObjString *s = AS_STRING(args[0]);
    int start = 0;
    int end = s->length;
    while (end > start && isWhitespace(s->chars[end - 1])) end--;
    if (start == end) {
        /* All whitespace: return a fresh empty string. */
        ObjString *result = copyString("", 0);
        return OBJ_VAL(result);
    }
    ObjString *result = copyString(s->chars + start, end - start);
    return OBJ_VAL(result);
}

/* --- Stage 21: string_repeat --- */
/* string_repeat(s, n) -> string. Concatenate s with itself n times.
 * n = 0 -> empty string (the "repeat zero times" idiom). Negative n
 * is a runtime error (a negative count is nonsensical; matches
 * string_substring's discipline of rejecting bad numeric input).
 * Non-integer n is truncated to int (matches string_substring's
 * (int) cast). The function allocates one buffer of size
 * s->length * n and copies in n passes; no intermediate ObjStrings,
 * no GC pressure. The product is checked for overflow — a
 * hypothetical user asking for billions of copies of a long string
 * gets a clear runtime error rather than a silent truncation. */
static Value stringRepeatNative(int argCount, Value *args) {
    if (argCount != 2) {
        runtimeError("string_repeat() takes 2 arguments (%d given).", argCount);
        return NIL_VAL;
    }
    if (!IS_STRING(args[0])) {
        runtimeError("string_repeat() argument 0 must be a string.");
        return NIL_VAL;
    }
    if (!IS_NUMBER(args[1])) {
        runtimeError("string_repeat() argument 1 must be a number.");
        return NIL_VAL;
    }
    ObjString *s = AS_STRING(args[0]);
    double nD = AS_NUMBER(args[1]);
    /* Truncate toward zero to match string_substring. Negative -> error. */
    int n = (int)nD;
    if ((double)n != nD) {
        /* nD had a fractional part; truncation is fine, no error. */
    }
    if (nD < 0) {
        runtimeError("string_repeat() argument 1 must be non-negative.");
        return NIL_VAL;
    }
    if (n == 0 || s->length == 0) {
        /* Either "zero repeats" or "any number of empty strings" -> "". */
        ObjString *result = copyString("", 0);
        return OBJ_VAL(result);
    }
    /* Overflow guard: a s->length * n that overflows int is a
     * runtime error, not a silent wrap. 2 GiB of string is well
     * beyond any realistic Lox use case. */
    if ((long long)s->length * (long long)n > INT_MAX) {
        runtimeError("string_repeat() result size exceeds maximum.");
        return NIL_VAL;
    }
    int outLen = s->length * n;
    char *buf = ALLOCATE(char, outLen + 1);
    /* n passes, each copying s->length bytes. The output buffer is
     * GC-managed via the ALLOCATE / FREE_ARRAY pattern; copyString
     * interns the result so repeated calls with the same input
     * return the same ObjString pointer. */
    for (int i = 0; i < n; i++) {
        memcpy(buf + i * s->length, s->chars, s->length);
    }
    buf[outLen] = '\0';
    ObjString *result = copyString(buf, outLen);
    FREE_ARRAY(char, buf, outLen + 1);
    return OBJ_VAL(result);
}

/* --- Stage 22: string_pad_start --- */
/* string_pad_start(s, width, fill) -> string. Pad s on the left with
 * copies of fill until the result is at least width characters.
 * If s->length >= width, returns s unchanged (Python convention:
 * never truncate, never error on "already wide enough"). Negative
 * width is a runtime error (matches string_repeat / string_substring's
 * discipline: bad numeric input is a runtime error, not silent).
 * Empty fill is a runtime error: padding with nothing is
 * nonsensical; if the caller wanted to truncate, they should use
 * string_substring. Fractional width is truncated to int (matches
 * string_repeat / string_substring's cast). Single allocation: one
 * output buffer of (width) bytes, filled in three passes (fill
 * copies first, then s copy) — no intermediate ObjStrings, no GC
 * pressure. If width > s->length, the total length is width and we
 * repeat fill to make up the difference. Overflow guard: the total
 * output size is bounded by width (a runtime int), which is already
 * bounded by INT_MAX. */
static Value stringPadStartNative(int argCount, Value *args) {
    if (argCount != 3) {
        runtimeError("string_pad_start() takes 3 arguments (%d given).", argCount);
        return NIL_VAL;
    }
    if (!IS_STRING(args[0])) {
        runtimeError("string_pad_start() argument 0 must be a string.");
        return NIL_VAL;
    }
    if (!IS_NUMBER(args[1])) {
        runtimeError("string_pad_start() argument 1 must be a number.");
        return NIL_VAL;
    }
    if (!IS_STRING(args[2])) {
        runtimeError("string_pad_start() argument 2 must be a string.");
        return NIL_VAL;
    }
    ObjString *s = AS_STRING(args[0]);
    double wD = AS_NUMBER(args[1]);
    ObjString *fill = AS_STRING(args[2]);
    int width = (int)wD;
    if (wD < 0) {
        runtimeError("string_pad_start() argument 1 must be non-negative.");
        return NIL_VAL;
    }
    if (fill->length == 0) {
        runtimeError("string_pad_start() argument 2 must be a non-empty string.");
        return NIL_VAL;
    }
    if (s->length >= width) {
        /* Already wide enough; return s unchanged. Returning the
         * input ObjString pointer is the same pattern string_substring
         * uses for its no-clamp-needed path. */
        return OBJ_VAL(s);
    }
    /* Need to pad: total length is width, of which s->length is the
     * tail and (width - s->length) is the prefix of fill copies.
     * (width - s->length) is positive because of the s->length >=
     * width check above, and bounded by width (an int). */
    int padLen = width - s->length;
    char *buf = ALLOCATE(char, width + 1);
    /* Fill the prefix with copies of fill. padLen / fill->length
     * is the number of full copies; padLen % fill->length is the
     * remainder (a partial fill at the end of the prefix). */
    int fullCopies = padLen / fill->length;
    int remainder = padLen - fullCopies * fill->length;
    int pos = 0;
    for (int i = 0; i < fullCopies; i++) {
        memcpy(buf + pos, fill->chars, fill->length);
        pos += fill->length;
    }
    if (remainder > 0) {
        memcpy(buf + pos, fill->chars, remainder);
        pos += remainder;
    }
    /* Append s. */
    memcpy(buf + pos, s->chars, s->length);
    buf[width] = '\0';
    ObjString *result = copyString(buf, width);
    FREE_ARRAY(char, buf, width + 1);
    return OBJ_VAL(result);
}

/* --- Stage 23: string_pad_end --- */
/* string_pad_end(s, width, fill) -> string. Mirror of string_pad_start:
 * pad s on the RIGHT with copies of fill until the result is at
 * least width characters. JavaScript's String.prototype.padEnd
 * semantics (Python's str.ljust doesn't support multi-char fill,
 * so JS is the canonical reference). If s->length >= width, returns
 * s unchanged (JS padEnd convention: never truncate, never error
 * on "already wide enough"). Negative width is a runtime error
 * (matches string_pad_start / string_repeat / string_substring's
 * discipline: bad numeric input is a runtime error, not silent).
 * Empty fill is a runtime error: padding with nothing is
 * nonsensical; if the caller wanted to truncate, they should use
 * string_substring. Fractional width is truncated to int (matches
 * string_pad_start / string_repeat / string_substring's cast).
 * Single allocation: one output buffer of (width) bytes, filled in
 * two passes (s copy first, then fill copies) — no intermediate
 * ObjStrings, no GC pressure. The total output size is bounded by
 * width (a runtime int), which is already bounded by INT_MAX. */
static Value stringPadEndNative(int argCount, Value *args) {
    if (argCount != 3) {
        runtimeError("string_pad_end() takes 3 arguments (%d given).", argCount);
        return NIL_VAL;
    }
    if (!IS_STRING(args[0])) {
        runtimeError("string_pad_end() argument 0 must be a string.");
        return NIL_VAL;
    }
    if (!IS_NUMBER(args[1])) {
        runtimeError("string_pad_end() argument 1 must be a number.");
        return NIL_VAL;
    }
    if (!IS_STRING(args[2])) {
        runtimeError("string_pad_end() argument 2 must be a string.");
        return NIL_VAL;
    }
    ObjString *s = AS_STRING(args[0]);
    double wD = AS_NUMBER(args[1]);
    ObjString *fill = AS_STRING(args[2]);
    int width = (int)wD;
    if (wD < 0) {
        runtimeError("string_pad_end() argument 1 must be non-negative.");
        return NIL_VAL;
    }
    if (fill->length == 0) {
        runtimeError("string_pad_end() argument 2 must be a non-empty string.");
        return NIL_VAL;
    }
    if (s->length >= width) {
        /* Already wide enough; return s unchanged. Returning the
         * input ObjString pointer is the same pattern string_substring
         * / string_pad_start uses for its no-clamp-needed path. */
        return OBJ_VAL(s);
    }
    /* Need to pad: total length is width, of which s->length is the
     * head and (width - s->length) is the suffix of fill copies.
     * (width - s->length) is positive because of the s->length >=
     * width check above, and bounded by width (an int). */
    int padLen = width - s->length;
    char *buf = ALLOCATE(char, width + 1);
    /* Copy s to the head of the buffer. */
    memcpy(buf, s->chars, s->length);
    /* Fill the suffix with copies of fill, starting at s->length.
     * padLen / fill->length is the number of full copies; padLen %
     * fill->length is the remainder (a partial fill at the end of
     * the suffix). */
    int fullCopies = padLen / fill->length;
    int remainder = padLen - fullCopies * fill->length;
    int pos = s->length;
    for (int i = 0; i < fullCopies; i++) {
        memcpy(buf + pos, fill->chars, fill->length);
        pos += fill->length;
    }
    if (remainder > 0) {
        memcpy(buf + pos, fill->chars, remainder);
        pos += remainder;
    }
    buf[width] = '\0';
    ObjString *result = copyString(buf, width);
    FREE_ARRAY(char, buf, width + 1);
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

/* --- Stage 11: I/O natives. The host-boundary lesson:
 *   - io_print(s) writes to stdout (no trailing newline; same channel
 *     as the implicit print() but does NOT add a newline like print() does).
 *   - io_eprint(s) writes to stderr (no trailing newline).
 *   - io_read_line() reads a line from stdin. Requires the script to be
 *     run with a non-empty stdin. Our test framework uses popen() which
 *     doesn't make stdin easy to control, so this is exercised in the
 *     REPL via a manual smoke test, not via a test.
 *   - io_exit(code) terminates the script with the given exit code.
 *     Implemented by storing the requested code in a global and reading
 *     it from interpret() in main.c. The VM has no other way to signal
 *     "exit early" without throwing through the call stack. */

#include <stdio.h>

/* Defined in main.c. The VM uses this to short-circuit the loop on exit. */
extern int g_exitRequested;
extern int g_exitCode;

static Value ioPrintNative(int argCount, Value *args) {
    if (argCount != 1) {
        runtimeError("io_print() takes 1 argument (%d given).", argCount);
        return NIL_VAL;
    }
    if (!IS_STRING(args[0])) {
        runtimeError("io_print() argument must be a string.");
        return NIL_VAL;
    }
    ObjString *s = AS_STRING(args[0]);
    fwrite(s->chars, 1, s->length, stdout);
    return NIL_VAL;
}

static Value ioEprintNative(int argCount, Value *args) {
    if (argCount != 1) {
        runtimeError("io_eprint() takes 1 argument (%d given).", argCount);
        return NIL_VAL;
    }
    if (!IS_STRING(args[0])) {
        runtimeError("io_eprint() argument must be a string.");
        return NIL_VAL;
    }
    ObjString *s = AS_STRING(args[0]);
    fwrite(s->chars, 1, s->length, stderr);
    return NIL_VAL;
}

/* io_exit(code) — terminate the script with the given exit code.
 * Sets a global flag interpreted by interpret() in main.c. */
static Value ioExitNative(int argCount, Value *args) {
    if (argCount != 1) {
        runtimeError("io_exit() takes 1 argument (%d given).", argCount);
        return NIL_VAL;
    }
    if (!IS_NUMBER(args[0])) {
        runtimeError("io_exit() argument must be a number.");
        return NIL_VAL;
    }
    g_exitRequested = 1;
    g_exitCode = (int)AS_NUMBER(args[0]);
    return NIL_VAL;
}

/* io_read_line() — read a single line from stdin (no trailing newline).
 * Returns nil on EOF. The buffer is bounded (1024 bytes) — a line longer
 * than that is truncated, which matches the REPL's input behavior. */
static Value ioReadLineNative(int argCount, Value *args) {
    if (argCount != 0) {
        runtimeError("io_read_line() takes 0 arguments (%d given).", argCount);
        return NIL_VAL;
    }
    (void)args;  /* unused */
    static char line[1024];
    if (fgets(line, sizeof(line), stdin) == NULL) {
        return NIL_VAL;  /* EOF or error */
    }
    /* Strip the trailing newline, if any. */
    size_t len = strlen(line);
    if (len > 0 && line[len - 1] == '\n') line[--len] = '\0';
    if (len > 0 && line[len - 1] == '\r') line[--len] = '\0';  /* CRLF */
    return OBJ_VAL(copyString(line, (int)len));
}

/* --- Stage 14: more I/O natives. The host-boundary lesson from
 *   Stage 11 still applies: bounded buffers, errors surface at the
 *   host boundary, the VM loop is unaware of file state. The new
 *   natives are:
 *     - io_read_file(path) -> string | nil
 *     - io_write_file(path, contents) -> nil
 *     - io_file_exists(path) -> bool
 *   File I/O errors (file not found, permission denied) surface as
 *   nil return values for read/exists and silent no-ops for write.
 *   Arity and type errors are runtime errors (consistent with the
 *   other natives). */

#include <sys/stat.h>   /* stat, S_ISREG */

/* Upper bound on file size we'll slurp into a clox string. Files
 * larger than this are not loadable. A real VM would do streaming
 * I/O for large files, but clox strings are fine for moderate
 * config-sized reads (logs, JSON, source code). 1 MiB is a
 * conservative bound for a stage 14 stdlib. */
#define IO_READ_FILE_MAX_BYTES (1 << 20)

/* io_read_file(path) -> string | nil.
 * Reads the entire file at `path` into a string. Returns nil on
 * any I/O error (file not found, permission denied, file too large,
 * fopen failure, etc). The size cap is enforced via fseek/ftell. */
static Value ioReadFileNative(int argCount, Value *args) {
    if (argCount != 1) {
        runtimeError("io_read_file() takes 1 argument (%d given).", argCount);
        return NIL_VAL;
    }
    if (!IS_STRING(args[0])) {
        runtimeError("io_read_file() argument must be a string.");
        return NIL_VAL;
    }
    ObjString *path = AS_STRING(args[0]);

    /* fopen wants a NUL-terminated string. Copy from the clox string. */
    char *pathC = malloc(path->length + 1);
    if (pathC == NULL) {
        return NIL_VAL;  /* OOM is its own host-boundary failure */
    }
    memcpy(pathC, path->chars, path->length);
    pathC[path->length] = '\0';

    FILE *f = fopen(pathC, "rb");
    free(pathC);
    if (f == NULL) {
        return NIL_VAL;  /* file not found, permission denied, etc. */
    }

    /* Measure the file. Reject anything that looks like a special
     * file (pipe, socket, device) or that exceeds our size cap. */
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NIL_VAL;
    }
    long size = ftell(f);
    if (size < 0 || size > IO_READ_FILE_MAX_BYTES) {
        fclose(f);
        return NIL_VAL;
    }
    rewind(f);

    /* Slurp the file into a stack buffer if it fits, else malloc.
     * For clox stdlib purposes, the file is small enough to slurp. */
    char stackBuf[8192];
    char *buf = (size <= (long)sizeof(stackBuf)) ? stackBuf : malloc((size_t)size);
    if (buf == NULL) {
        fclose(f);
        return NIL_VAL;
    }

    size_t n = fread(buf, 1, (size_t)size, f);
    fclose(f);
    if (n != (size_t)size) {
        if (buf != stackBuf) free(buf);
        return NIL_VAL;
    }

    /* copyString copies the bytes into a clox ObjString. We free our
     * temporary buffer after the copy is in the heap. */
    Value result = OBJ_VAL(copyString(buf, (int)n));
    if (buf != stackBuf) free(buf);
    return result;
}

/* io_write_file(path, contents) -> nil.
 * Writes `contents` to `path`, overwriting if it exists. Returns nil
 * on success. Returns nil on I/O failure (silent — consistent with
 * io_read_file's "nil on error" convention). The arity/type errors
 * are runtime errors. */
static Value ioWriteFileNative(int argCount, Value *args) {
    if (argCount != 2) {
        runtimeError("io_write_file() takes 2 arguments (%d given).", argCount);
        return NIL_VAL;
    }
    if (!IS_STRING(args[0]) || !IS_STRING(args[1])) {
        runtimeError("io_write_file() arguments must be (string, string).");
        return NIL_VAL;
    }
    ObjString *path     = AS_STRING(args[0]);
    ObjString *contents = AS_STRING(args[1]);

    char *pathC = malloc(path->length + 1);
    if (pathC == NULL) return NIL_VAL;
    memcpy(pathC, path->chars, path->length);
    pathC[path->length] = '\0';

    FILE *f = fopen(pathC, "wb");
    free(pathC);
    if (f == NULL) {
        return NIL_VAL;  /* permission denied, invalid path, etc. */
    }

    size_t written = fwrite(contents->chars, 1, contents->length, f);
    fclose(f);
    if (written != (size_t)contents->length) {
        return NIL_VAL;  /* disk full, etc. */
    }
    return NIL_VAL;
}

/* io_file_exists(path) -> bool.
 * Returns true if a regular file exists at `path`, false otherwise.
 * Uses stat() which works on the symlink target (consistent with
 * what users expect from a "does this file exist" check). */
static Value ioFileExistsNative(int argCount, Value *args) {
    if (argCount != 1) {
        runtimeError("io_file_exists() takes 1 argument (%d given).", argCount);
        return NIL_VAL;
    }
    if (!IS_STRING(args[0])) {
        runtimeError("io_file_exists() argument must be a string.");
        return NIL_VAL;
    }
    ObjString *path = AS_STRING(args[0]);

    char *pathC = malloc(path->length + 1);
    if (pathC == NULL) return BOOL_VAL(false);  /* OOM -> "doesn't exist" */
    memcpy(pathC, path->chars, path->length);
    pathC[path->length] = '\0';

    struct stat st;
    int result = stat(pathC, &st);
    free(pathC);
    if (result != 0) {
        return BOOL_VAL(false);  /* file doesn't exist or unreachable */
    }
    /* Only return true for regular files. A directory or device
     * at `path` is not "a file" for this check. */
    return BOOL_VAL(S_ISREG(st.st_mode));
}

/* --- Stage 16: streaming I/O. The new natives are:
 *   - io_read_lines(path) -> ObjArray | nil
 *   - io_write_lines(path, arr) -> nil
 *
 * These compose the file I/O from Stage 14 with the array
 * primitives from Stage 12/13. A file is an array of lines;
 * an array of lines is a file. This is the natural extension
 * of "host boundary expansion" from Stage 14: not just files,
 * but *structured* files.
 *
 * The implementation reuses the Stage 14 io_read_file and
 * io_write_file natives' machinery where possible. The new
 * work is the line-splitting and line-joining. */

#include <string.h>  /* memcpy, memchr */

/* Helper: find the next '\n' in `buf` starting at `start`. Returns
 * the index of the newline, or `len` if no newline is found. */
static int findNewline(const char *buf, int start, int len) {
    for (int i = start; i < len; i++) {
        if (buf[i] == '\n') return i;
    }
    return len;
}

/* io_read_lines(path) -> ObjArray | nil.
 * Reads the file at `path` and splits its contents on '\n' into
 * an array of strings. The trailing empty piece (from a trailing
 * newline) is dropped — `io_read_lines` returns *lines*, not
 * *newlines*. So a file "a\nb\nc\n" becomes ["a", "b", "c"],
 * matching `wc -l` semantics.
 *
 * If the file can't be read (nonexistent, permission denied,
 * too large), returns nil (consistent with io_read_file). */
static Value ioReadLinesNative(int argCount, Value *args) {
    if (argCount != 1) {
        runtimeError("io_read_lines() takes 1 argument (%d given).", argCount);
        return NIL_VAL;
    }
    if (!IS_STRING(args[0])) {
        runtimeError("io_read_lines() argument must be a string.");
        return NIL_VAL;
    }
    ObjString *path = AS_STRING(args[0]);

    char *pathC = malloc(path->length + 1);
    if (pathC == NULL) return NIL_VAL;
    memcpy(pathC, path->chars, path->length);
    pathC[path->length] = '\0';

    FILE *f = fopen(pathC, "rb");
    free(pathC);
    if (f == NULL) return NIL_VAL;  /* file not found, permission denied, etc. */

    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NIL_VAL; }
    long size = ftell(f);
    if (size < 0 || size > IO_READ_FILE_MAX_BYTES) { fclose(f); return NIL_VAL; }
    rewind(f);

    char stackBuf[8192];
    char *buf = (size <= (long)sizeof(stackBuf)) ? stackBuf : malloc((size_t)size);
    if (buf == NULL) { fclose(f); return NIL_VAL; }

    size_t n = fread(buf, 1, (size_t)size, f);
    fclose(f);
    if (n != (size_t)size) {
        if (buf != stackBuf) free(buf);
        return NIL_VAL;
    }

    /* Worst case: every byte is a newline, so size+1 pieces. Plus
     * one for the trailing piece (which we'll drop). */
    ObjArray *array = newArray((int)n + 1);
    push(OBJ_VAL(array));  /* GC protection during fill */

    int start = 0;
    int i = 0;
    while (start < (int)n) {
        i = findNewline(buf, start, (int)n);
        /* Copy buf[start..i] (exclusive of the newline). */
        ObjString *line = copyString(buf + start, i - start);
        arrayPush(array, OBJ_VAL(line));
        start = i + 1;  /* skip the newline */
    }
    /* If the file ended at a newline, the last "line" would be
     * empty. Drop it. The file "a\nb\nc\n" -> 3 lines, not 4. */
    if (array->count > 0) {
        Value last = arrayRead(array, array->count - 1);
        if (AS_STRING(last)->length == 0) {
            array->count--;  /* drop trailing empty */
        }
    }

    pop();  /* release GC protection */
    if (buf != stackBuf) free(buf);
    return OBJ_VAL(array);
}

/* io_write_lines(path, arr) -> nil.
 * Writes the array of strings to `path`, one per line, joined by
 * '\n'. The output always has a trailing newline. Empty array
 * produces an empty file. Non-string elements are a runtime
 * error (consistent with string_join's behavior). */
static Value ioWriteLinesNative(int argCount, Value *args) {
    if (argCount != 2) {
        runtimeError("io_write_lines() takes 2 arguments (%d given).", argCount);
        return NIL_VAL;
    }
    if (!IS_STRING(args[0]) || !IS_ARRAY(args[1])) {
        runtimeError("io_write_lines() arguments must be (string, array).");
        return NIL_VAL;
    }
    ObjString *path = AS_STRING(args[0]);
    ObjArray  *arr  = AS_ARRAY(args[1]);

    /* Build the output by string_joining the array on '\n', then
     * appending a trailing '\n'. The "+ '\n'" makes empty arrays
     * produce empty files (not files with a single newline) and
     * makes non-empty arrays end with a newline. */
    if (arr->count == 0) {
        /* Empty array: write nothing. */
        char *pathC = malloc(path->length + 1);
        if (pathC == NULL) return NIL_VAL;
        memcpy(pathC, path->chars, path->length);
        pathC[path->length] = '\0';
        FILE *f = fopen(pathC, "wb");
        free(pathC);
        if (f == NULL) return NIL_VAL;
        fclose(f);
        return NIL_VAL;
    }

    /* Compute the total output size. */
    int total = 0;
    for (int i = 0; i < arr->count; i++) {
        Value element = arrayRead(arr, i);
        if (!IS_STRING(element)) {
            runtimeError("io_write_lines() array element %d is not a string.", i);
            return NIL_VAL;
        }
        total += AS_STRING(element)->length;
    }
    total += (arr->count - 1);  /* (n-1) newline separators */
    total += 1;                 /* trailing newline */

    char *out = ALLOCATE(char, total + 1);
    char *p = out;
    for (int i = 0; i < arr->count; i++) {
        Value element = arrayRead(arr, i);
        ObjString *s = AS_STRING(element);
        memcpy(p, s->chars, s->length);
        p += s->length;
        if (i < arr->count - 1) {
            *p++ = '\n';
        } else {
            *p++ = '\n';  /* trailing newline */
        }
    }
    *p = '\0';

    /* Write the buffer to the file. */
    char *pathC = malloc(path->length + 1);
    if (pathC == NULL) { FREE_ARRAY(char, out, total + 1); return NIL_VAL; }
    memcpy(pathC, path->chars, path->length);
    pathC[path->length] = '\0';

    FILE *f = fopen(pathC, "wb");
    free(pathC);
    if (f == NULL) { FREE_ARRAY(char, out, total + 1); return NIL_VAL; }

    size_t written = fwrite(out, 1, (size_t)total, f);
    fclose(f);
    FREE_ARRAY(char, out, total + 1);
    if (written != (size_t)total) return NIL_VAL;
    return NIL_VAL;
}

/* --- Stage 12a: array value type via natives. The value type
 *   (ObjArray) is in object.h; the GC mark/sweep is in gc.c. The
 *   natives below are the only way a Lox program can create or
 *   manipulate an array in this stage. Stage 12b will add the
 *   [1, 2, 3] literal and a[i] index access at the language level. */

/* --- Stage 13: string_split / string_join --- */

static Value stringSplitNative(int argCount, Value *args) {
    /* string_split(s, delim) -> ObjArray of substrings. Algorithm:
     * track a "prev" pointer to the start of the next piece. At each
     * position, check if the delimiter starts here; if yes, push the
     * piece s[prev..i] and advance past the delim. When the scan
     * completes, push the tail s[prev..s->length] (which may be empty
     * if the string ended at a delim). For empty input s, return [].
     * Edge cases:
     *   string_split("",  ",") -> []        (empty input)
     *   string_split("a", ",") -> ["a"]     (no delim found)
     *   string_split("a,", ",") -> ["a", ""]  (trailing empty)
     *   string_split(",", ",") -> ["", ""]  (empty before, empty after)
     *   string_split("a", "")  -> ["a"]     (empty delim = whole string)
     *
     * --- Stage 29: string_split(s, delim, limit) --- */
    /* Extends Stage 13 with a max-split-count parameter. 2 args
     * (Stage 13) splits on every occurrence of delim; 3 args
     * (Stage 29) splits at most `limit` times, leaving the rest
     * of the string as the final element. JS reference:
     * String.prototype.split(s, limit) — limit is optional,
     * default is "split on every occurrence." Python reference:
     * str.split(sep, maxsplit) — maxsplit is optional, default
     * is -1 (no limit). The natural small-mirror's mirror is
     * "add one parameter to an existing function" rather than
     * "new conceptual native."
     *
     * Edge cases for limit:
     *   limit = 0  -> [s] (no splits; whole string is one element)
     *   limit < 0  -> runtime error
     *   limit = 1  -> split at most once -> 2 elements max
     *   limit > #  -> full split (no limit reached) */
    if (argCount != 2 && argCount != 3) {
        runtimeError("string_split() takes 2 or 3 arguments (%d given).", argCount);
        return NIL_VAL;
    }
    if (!IS_STRING(args[0]) || !IS_STRING(args[1])) {
        runtimeError("string_split() arguments 0 and 1 must be strings.");
        return NIL_VAL;
    }
    int limit = -1;  /* -1 = no limit (Stage 13 default behavior) */
    if (argCount == 3) {
        if (!IS_NUMBER(args[2])) {
            runtimeError("string_split() argument 2 must be a number.");
            return NIL_VAL;
        }
        double limitD = AS_NUMBER(args[2]);
        int limitI = (int)limitD;
        if ((double)limitI != limitD) {
            /* Fractional limit: not an integer. Error per the
             * 'strict on numeric input' discipline (matches
             * string_repeat's discipline of rejecting fractional
             * counts). */
            runtimeError("string_split() argument 2 must be an integer.");
            return NIL_VAL;
        }
        if (limitI < 0) {
            runtimeError("string_split() argument 2 must be non-negative.");
            return NIL_VAL;
        }
        limit = limitI;
    }
    ObjString *s     = AS_STRING(args[0]);
    ObjString *delim = AS_STRING(args[1]);

    /* Upper bound: in the worst case (alternating chars and delim), we
     * get s->length / delim->length + 1 elements. For delim->length ==
     * 0 we want a single element (the whole string), per spec above. */
    int worstCase = (delim->length > 0)
                        ? (s->length / delim->length + 1)
                        : 1;
    ObjArray *array = newArray(worstCase);
    push(OBJ_VAL(array));  /* GC: keep alive while filling */

    if (s->length == 0) {
        /* Empty input: empty array. */
        pop();
        return OBJ_VAL(array);
    }

    if (delim->length == 0) {
        /* Empty delim: the whole string is one element. */
        ObjString *whole = copyString(s->chars, s->length);
        arrayPush(array, OBJ_VAL(whole));
        pop();
        return OBJ_VAL(array);
    }

    int prev = 0;
    int i = 0;
    int splitsDone = 0;
    while (i <= s->length - delim->length) {
        /* If limit is set and we've reached it, stop. The remaining
         * text (from prev to s->length) is the final element. */
        if (limit >= 0 && splitsDone >= limit) break;
        bool match = true;
        for (int j = 0; j < delim->length; j++) {
            if (s->chars[i + j] != delim->chars[j]) {
                match = false;
                break;
            }
        }
        if (match) {
            /* Push the piece s[prev..i]. */
            int pieceLen = i - prev;
            ObjString *part = copyString(s->chars + prev, pieceLen);
            arrayPush(array, OBJ_VAL(part));
            i += delim->length;
            prev = i;
            splitsDone++;
        } else {
            i++;
        }
    }
    /* Push the tail s[prev..s->length]. This is empty if the string
     * ended at a delim boundary (trailing empty), or contains the
     * remaining text if the limit was reached. */
    {
        int tailLen = s->length - prev;
        ObjString *tail = copyString(s->chars + prev, tailLen);
        arrayPush(array, OBJ_VAL(tail));
    }

    pop();
    return OBJ_VAL(array);
}

static Value stringJoinNative(int argCount, Value *args) {
    /* string_join(arr, delim) -> string. Walks the array, copies each
     * element to a buffer, in between copies the delim. Total length is
     * sum of element lengths + (n-1) * delim.length. */
    if (argCount != 2) {
        runtimeError("string_join() takes 2 arguments (%d given).", argCount);
        return NIL_VAL;
    }
    if (!IS_ARRAY(args[0]) || !IS_STRING(args[1])) {
        runtimeError("string_join() arguments must be (array, string).");
        return NIL_VAL;
    }
    ObjArray *arr   = AS_ARRAY(args[0]);
    ObjString *delim = AS_STRING(args[1]);

    /* Compute total length. Each element must be a string. */
    int total = 0;
    if (arr->count > 0) {
        total += (arr->count - 1) * delim->length;
        for (int i = 0; i < arr->count; i++) {
            Value element = arrayRead(arr, i);
            if (!IS_STRING(element)) {
                runtimeError("string_join() array element %d is not a string.", i);
                return NIL_VAL;
            }
            total += AS_STRING(element)->length;
        }
    }

    char *buf = ALLOCATE(char, total + 1);
    /* No GC push needed here: buf is a raw char* (not a heap object),
     * and the input ObjStrings are already reachable via arr, which
     * is held by the caller's stack frame. copyString() may allocate
     * a new ObjString for the result, but that becomes the return
     * value, not a stack-temporary. */
    int pos = 0;
    for (int i = 0; i < arr->count; i++) {
        if (i > 0) {
            memcpy(buf + pos, delim->chars, delim->length);
            pos += delim->length;
        }
        ObjString *element = AS_STRING(arrayRead(arr, i));
        memcpy(buf + pos, element->chars, element->length);
        pos += element->length;
    }
    buf[total] = '\0';

    ObjString *result = copyString(buf, total);
    FREE_ARRAY(char, buf, total + 1);
    return OBJ_VAL(result);
}

static Value arrayCreateNative(int argCount, Value *args) {
    /* array(arg1, arg2, ...) -> ObjArray of the given args. */
    ObjArray *array = newArray(argCount);
    push(OBJ_VAL(array));  /* GC: keep alive while filling */
    for (int i = 0; i < argCount; i++) {
        arrayPush(array, args[i]);
    }
    pop();
    return OBJ_VAL(array);
}

static Value arrayLengthNative(int argCount, Value *args) {
    if (argCount != 1) {
        runtimeError("array_length() takes 1 argument (%d given).", argCount);
        return NIL_VAL;
    }
    if (!IS_ARRAY(args[0])) {
        runtimeError("array_length() argument must be an array.");
        return NIL_VAL;
    }
    return NUMBER_VAL((double)AS_ARRAY(args[0])->count);
}

static Value arrayGetNative(int argCount, Value *args) {
    if (argCount != 2) {
        runtimeError("array_get() takes 2 arguments (%d given).", argCount);
        return NIL_VAL;
    }
    if (!IS_ARRAY(args[0])) {
        runtimeError("array_get() first argument must be an array.");
        return NIL_VAL;
    }
    if (!IS_NUMBER(args[1])) {
        runtimeError("array_get() second argument must be a number.");
        return NIL_VAL;
    }
    ObjArray *array = AS_ARRAY(args[0]);
    int index = (int)AS_NUMBER(args[1]);
    if (index < 0 || index >= array->count) {
        runtimeError("array_get() index %d out of bounds (length %d).",
                     index, array->count);
        return NIL_VAL;
    }
    return arrayRead(array, index);
}

static Value arraySetNative(int argCount, Value *args) {
    if (argCount != 3) {
        runtimeError("array_set() takes 3 arguments (%d given).", argCount);
        return NIL_VAL;
    }
    if (!IS_ARRAY(args[0])) {
        runtimeError("array_set() first argument must be an array.");
        return NIL_VAL;
    }
    if (!IS_NUMBER(args[1])) {
        runtimeError("array_set() second argument must be a number.");
        return NIL_VAL;
    }
    ObjArray *array = AS_ARRAY(args[0]);
    int index = (int)AS_NUMBER(args[1]);
    if (index < 0 || index >= array->count) {
        runtimeError("array_set() index %d out of bounds (length %d).",
                     index, array->count);
        return NIL_VAL;
    }
    arrayWrite(array, index, args[2]);
    return NIL_VAL;
}

static Value arrayPushNative(int argCount, Value *args) {
    if (argCount != 2) {
        runtimeError("array_push() takes 2 arguments (%d given).", argCount);
        return NIL_VAL;
    }
    if (!IS_ARRAY(args[0])) {
        runtimeError("array_push() first argument must be an array.");
        return NIL_VAL;
    }
    /* Stage 20: return the new length instead of nil. The
     * "return the natural value" convention: for a push, the
     * natural return value is the new length. Existing callers
     * that ignore the return value are unaffected. */
    ObjArray *array = AS_ARRAY(args[0]);
    arrayPush(array, args[1]);
    return NUMBER_VAL((double)array->count);
}

/* Stage 19: array_reverse(arr) -> arr
 *
 * Reverses the array in place and returns the same array, so calls
 * can chain. The return-the-array convention differs from
 * array_push (which returns nil) because the natural return value
 * of a reverse is the reversed array, not a void. Users who want
 * statement-style mutation write `array_reverse(a); print a;` and
 * ignore the return value. Users who want expression-style
 * chaining write `print(array_reverse(a));`.
 *
 * The reversal is an in-place swap loop: swap elements[i] with
 * elements[count-1-i] for i in 0..count/2. No allocation, no GC
 * concern — the elements are Values (8 bytes each on 64-bit),
 * the swap is just moving two pointers. */
static Value arrayReverseNative(int argCount, Value *args) {
    if (argCount != 1) {
        runtimeError("array_reverse() takes 1 argument (%d given).", argCount);
        return NIL_VAL;
    }
    if (!IS_ARRAY(args[0])) {
        runtimeError("array_reverse() argument must be an array.");
        return NIL_VAL;
    }
    ObjArray *array = AS_ARRAY(args[0]);
    int i = 0;
    int j = array->count - 1;
    while (i < j) {
        Value tmp = array->elements[i];
        array->elements[i] = array->elements[j];
        array->elements[j] = tmp;
        i++;
        j--;
    }
    return args[0];
}

/* Stage 28: array_unique(arr) -> array
 *
 * Returns a new array containing only the first occurrence of each
 * distinct value, preserving input order. Uses clox's valuesEqual()
 * semantics (type-sensitive; object identity for objects, which for
 * interned strings means content-equal literals collapse).
 *
 * Implementation: walk the input once; for each element, scan the
 * result built so far and append only if not already present. Worst
 * case O(n^2) comparisons, acceptable for the "hand-rolled dedup"
 * lesson stage. Allocates one new ObjArray with capacity = input
 * count; actual count may be smaller. */
static Value arrayUniqueNative(int argCount, Value *args) {
    if (argCount != 1) {
        runtimeError("array_unique() takes 1 argument (%d given).", argCount);
        return NIL_VAL;
    }
    if (!IS_ARRAY(args[0])) {
        runtimeError("array_unique() argument must be an array.");
        return NIL_VAL;
    }
    ObjArray *input = AS_ARRAY(args[0]);
    ObjArray *result = newArray(input->count);
    push(OBJ_VAL(result));  /* GC: keep alive while filling */
    for (int i = 0; i < input->count; i++) {
        Value candidate = input->elements[i];
        bool found = false;
        for (int j = 0; j < result->count; j++) {
            if (valuesEqual(candidate, result->elements[j])) {
                found = true;
                break;
            }
        }
        if (!found) {
            arrayPush(result, candidate);
        }
    }
    pop();
    return OBJ_VAL(result);
}

/* Stage 40: array_unique_by(arr, keyFn) -> array
 *
 * Returns a new array containing only the first occurrence of each
 * element, where "unique" is determined by the keyFn's return value
 * rather than the element itself. The keyFn is a 1-arg Lox closure
 * that takes the element and returns the key. Order is preserved
 * (the canonical "first-occurrence-of-each-key" rule, matches
 * lodash's _.uniqBy).
 *
 * Implementation: walk the input once; for each element, call the
 * keyFn to get the key, then scan the keys-so-far and append the
 * element only if its key is not already present. O(n^2) on the
 * key comparison; the keyFn is called once per element.
 *
 * Architecture: reuses Stage 30's callClosureFromNative(argCount=1)
 * — the verified 1-arg closure path. No new architecture work. */
static Value arrayUniqueByNative(int argCount, Value *args) {
    if (argCount != 2) {
        runtimeError("array_unique_by() takes 2 arguments (%d given).", argCount);
        return NIL_VAL;
    }
    if (!IS_ARRAY(args[0])) {
        runtimeError("array_unique_by() argument must be an array.");
        return NIL_VAL;
    }
    if (!IS_CLOSURE(args[1])) {
        runtimeError("array_unique_by() keyFn must be a function.");
        return NIL_VAL;
    }
    ObjArray *input = AS_ARRAY(args[0]);
    ObjClosure *keyFn = AS_CLOSURE(args[1]);
    ObjArray *result = newArray(input->count);
    push(OBJ_VAL(result));  /* GC: keep alive while filling */
    /* Stash the keys-so-far in a parallel array on the Lox heap
     * (a C-side Value array would need a GC-root too; the Lox
     * heap gives us push/pop GC protection for free). */
    ObjArray *seenKeys = newArray(input->count);
    push(OBJ_VAL(seenKeys));  /* GC: keep alive while filling */
    for (int i = 0; i < input->count; i++) {
        Value candidate = input->elements[i];
        /* Compute the key by calling keyFn(candidate). The
         * stack layout for callClosureFromNative is
         * [callee, arg1, ..., argN]; push them in order. */
        push(OBJ_VAL(keyFn));
        push(candidate);
        Value key = callClosureFromNative(keyFn, 1);
        /* Scan the seen keys to check if this key is already present. */
        bool found = false;
        for (int j = 0; j < seenKeys->count; j++) {
            if (valuesEqual(key, seenKeys->elements[j])) {
                found = true;
                break;
            }
        }
        if (!found) {
            arrayPush(seenKeys, key);
            arrayPush(result, candidate);
        }
    }
    pop();  /* seenKeys */
    pop();  /* result */
    return OBJ_VAL(result);
}

/* --- Stage 30: array_filter(arr, predicate) -> array --- */
/* The first native that invokes user-defined Lox code from C.
 * The shape: 2 args (array, predicate). The predicate is a
 * 1-arg Lox closure. Returns a new array containing only the
 * elements for which the predicate returns true (clox's
 * standard truthy/falsy rule: false and nil are falsy,
 * everything else is truthy). Order is preserved. The empty
 * input case returns []; the all-false case returns []; the
 * original array is not mutated.
 *
 * JS reference: Array.prototype.filter(predicate, thisArg)
 * Python reference: filter(function, iterable)
 * The clox semantics: predicate is called with one argument
 * (the element); the result is checked for truthiness.
 *
 * --- Architecture: user-code dispatch from a native ---
 * The native function signature is `Value (*)(int argCount,
 * Value *args)` where `args` points to the first arg on the
 * VM stack. To invoke a Lox closure from a native, we use
 * `call(closure, argCount)` from vm.c, now exposed in vm.h.
 * The contract:
 *   - args must point to the first arg on the VM stack (the
 *     native's args pointer)
 *   - call() will push the result onto the stack and return true
 *   - on runtime error (arity mismatch, stack overflow, callee-
 *     side error), runtimeError() does longjmp and call()'s
 *     return is unreachable
 *
 * The `call` function is the same one the OP_CALL bytecode
 * handler uses internally. Promoting it from static to public
 * is the smallest change that enables this and any future
 * native that needs to invoke user code.
 *
 * --- GC: keeping the result and the predicate alive ---
 * The result array is GC-allocated (newArray). The predicate
 * closure is borrowed from args[1] (already on the stack, so
 * reachable from the call frame). The current element is
 * pushed before call() (it's the call's argument) and the
 * result is popped immediately after to keep the stack
 * balanced.
 *
 * --- Why this stage is more than a new native ---
 * It's the architecture work that unlocks: array_map, array_reduce,
 * array_any, array_all, array_find — all of which need to invoke
 * a user-defined predicate. The pattern established here (call
 * a closure with args, get a result, pop the result) is the
 * template for all of them. */

static Value arrayFilterNative(int argCount, Value *args) {
    if (argCount != 2) {
        runtimeError("array_filter() takes 2 arguments (%d given).", argCount);
        return NIL_VAL;
    }
    if (!IS_ARRAY(args[0])) {
        runtimeError("array_filter() argument 0 must be an array.");
        return NIL_VAL;
    }
    if (!IS_CLOSURE(args[1])) {
        runtimeError("array_filter() argument 1 must be a function.");
        return NIL_VAL;
    }
    ObjArray *src = AS_ARRAY(args[0]);
    ObjClosure *predicate = AS_CLOSURE(args[1]);
    if (predicate->function->arity != 1) {
        runtimeError("array_filter() predicate must take 1 argument (got %d).",
                     predicate->function->arity);
        return NIL_VAL;
    }

    /* Allocate the result. Worst case: every element passes,
     * so capacity = src->count. The result is empty in the
     * "no element passes" case; the empty case for an empty
     * source is naturally handled. */
    ObjArray *result = newArray(src->count);
    push(OBJ_VAL(result));  /* GC: keep alive while filling */

    for (int i = 0; i < src->count; i++) {
        /* Stack layout for callClosure: [callee, arg1, ...].
         * The predicate is already on the stack (it's args[1] of
         * the native call), but we need to push it as the callee
         * for the predicate call. We do that by pushing the
         * predicate, then the element. The result_array stays on
         * the stack (under everything) for GC protection.
         *
         * Stack before: [..., src, predicate, result_array]
         * Stack after:  [..., src, predicate, result_array, predicate, element]
         *
         * callClosure(predicate, 1) sets frame->slots to the predicate
         * position (stackTop - 2), so slots[0] = predicate, slots[1] = element.
         * The predicate's bytecode accesses its arg at slots[1]. */
        push(OBJ_VAL(predicate));  /* callee */
        push(src->elements[i]);    /* arg 1 */
        Value filterResult = callClosureFromNative(predicate, 1);
        /* Clox's truthy/falsy rule: false and nil are falsy,
         * everything else truthy. IS_BOOL + AS_BOOL false,
         * or IS_NIL, are the two explicit falsy cases. */
        bool keep = !(IS_BOOL(filterResult) && !AS_BOOL(filterResult))
                 && !IS_NIL(filterResult);
        if (keep) {
            arrayPush(result, src->elements[i]);
        }
    }

    pop();  /* pop the result array (it stays on the stack until
             * the function returns; the VM will handle pushing
             * the return value) */
    return OBJ_VAL(result);
}

static Value arrayReduceNative(int argCount, Value *args) {
    if (argCount != 3) {
        runtimeError("array_reduce() takes 3 arguments (%d given).", argCount);
        return NIL_VAL;
    }
    if (!IS_ARRAY(args[0])) {
        runtimeError("array_reduce() argument 0 must be an array.");
        return NIL_VAL;
    }
    if (!IS_CLOSURE(args[1])) {
        runtimeError("array_reduce() argument 1 must be a function.");
        return NIL_VAL;
    }
    ObjArray *src = AS_ARRAY(args[0]);
    ObjClosure *reducer = AS_CLOSURE(args[1]);
    if (reducer->function->arity != 2) {
        runtimeError("array_reduce() reducer must take 2 arguments (got %d).",
                     reducer->function->arity);
        return NIL_VAL;
    }

    /* The accumulator starts as the initial value. args[2]
     * is on the native's stack; we copy it into a local
     * Value so the accumulator is a tracked C variable.
     * The rest of the native's stack is: [..., src, reducer].
     * The src and reducer are still pinned on the stack by
     * callValue's frame; we just need to manage the
     * accumulator ourselves. */
    Value acc = args[2];

    /* Empty array with no iterations: return the initial as-is. */
    for (int i = 0; i < src->count; i++) {
        /* Stack layout for callClosure: [callee, arg1, ..., argN].
         * We push the reducer (callee), then the accumulator
         * (arg 1), then the element (arg 2). The reducer's
         * bytecode accesses its args at slots[1] (acc) and
         * slots[2] (element). */
        push(OBJ_VAL(reducer));   /* callee */
        push(acc);                /* arg 1: accumulator */
        push(src->elements[i]);   /* arg 2: element */
        acc = callClosureFromNative(reducer, 2);
    }

    /* Return the final accumulator. No output array to GC-protect:
     * the result is a single Value (number, string, bool, nil, or
     * an object reference), and the native's frame already
     * pins the necessary objects (the reducer, the source). */
    return acc;
}

static Value arrayMapNative(int argCount, Value *args) {
    if (argCount != 2) {
        runtimeError("array_map() takes 2 arguments (%d given).", argCount);
        return NIL_VAL;
    }
    if (!IS_ARRAY(args[0])) {
        runtimeError("array_map() argument 0 must be an array.");
        return NIL_VAL;
    }
    if (!IS_CLOSURE(args[1])) {
        runtimeError("array_map() argument 1 must be a function.");
        return NIL_VAL;
    }
    ObjArray *src = AS_ARRAY(args[0]);
    ObjClosure *transform = AS_CLOSURE(args[1]);
    if (transform->function->arity != 1) {
        runtimeError("array_map() transform must take 1 argument (got %d).",
                     transform->function->arity);
        return NIL_VAL;
    }

    /* Allocate the result. map always returns one element per input
     * element, so capacity is exactly src->count. */
    ObjArray *result = newArray(src->count);
    push(OBJ_VAL(result));  /* GC: keep alive while filling */

    for (int i = 0; i < src->count; i++) {
        /* Stack layout for callClosure: [callee, arg1, ...].
         * The transform is already on the stack (it's args[1] of
         * the native call), but we need to push it as the callee
         * for the transform call. We do that by pushing the
         * transform, then the element. The result_array stays on
         * the stack (under everything) for GC protection.
         *
         * Stack before: [..., src, transform, result_array]
         * Stack after:  [..., src, transform, result_array, transform, element]
         *
         * callClosure(transform, 1) sets frame->slots to the transform
         * position (stackTop - 2), so slots[0] = transform, slots[1] = element.
         * The transform's bytecode accesses its arg at slots[1]. */
        push(OBJ_VAL(transform));  /* callee */
        push(src->elements[i]);    /* arg 1 */
        Value mapped = callClosureFromNative(transform, 1);
        /* Push the transform's return value to the result. The
         * GC is safe: the result array is on the stack below the
         * native's frames, and any allocations the transform made
         * are handled by its own frame's GC checkpoints. */
        arrayPush(result, mapped);
    }

    pop();  /* pop the result array (it stays on the stack until
             * the function returns; the VM will handle pushing
             * the return value) */
    return OBJ_VAL(result);
}

/* Stage 33: array_any(arr, predicate) -> bool.
 * Returns true as soon as the predicate is truthy on any
 * element (short-circuit); false if all elements are falsy.
 * The natural mirror of Stage 30's array_filter, with the
 * first deviation from the "iterate everything" pattern.
 * The architecture is the same (callClosureFromNative with
 * argCount=1); the only new thing is the early-exit return
 * on the first truthy predicate result. The empty-array
 * case returns false without ever calling the predicate
 * (matches JS Array.prototype.some and Python's any).
 *
 * Stack layout for callClosure: [callee, arg1, ...]. */
static Value arrayAnyNative(int argCount, Value *args) {
    if (argCount != 2) {
        runtimeError("array_any() takes 2 arguments (%d given).", argCount);
        return BOOL_VAL(false);
    }
    if (!IS_ARRAY(args[0])) {
        runtimeError("array_any() argument 0 must be an array.");
        return BOOL_VAL(false);
    }
    if (!IS_CLOSURE(args[1])) {
        runtimeError("array_any() argument 1 must be a function.");
        return BOOL_VAL(false);
    }
    ObjArray *src = AS_ARRAY(args[0]);
    ObjClosure *predicate = AS_CLOSURE(args[1]);
    if (predicate->function->arity != 1) {
        runtimeError("array_any() predicate must take 1 argument (got %d).",
                     predicate->function->arity);
        return BOOL_VAL(false);
    }

    for (int i = 0; i < src->count; i++) {
        /* Same stack layout as array_map: push predicate
         * (callee) then element (arg 1). The predicate's
         * bytecode reads its arg at slots[1]. */
        push(OBJ_VAL(predicate));  /* callee */
        push(src->elements[i]);    /* arg 1 */
        Value result = callClosureFromNative(predicate, 1);
        /* Short-circuit: as soon as the predicate returns a
         * truthy value, return true without iterating the
         * rest. clox's truthy rule: only false and nil are
         * falsy (not 0, not ""). */
        if (!(IS_BOOL(result) && !AS_BOOL(result)) && !IS_NIL(result)) {
            return BOOL_VAL(true);
        }
    }

    /* No truthy predicate result found: return false. */
    return BOOL_VAL(false);
}

/* Stage 34: array_all(arr, predicate) -> bool.
 * Returns false as soon as the predicate is falsy on any
 * element (short-circuit); true if all elements are truthy.
 * The natural mirror of Stage 33's array_any, with the
 * short-circuit direction reversed: short-circuit on the
 * *first falsy* (vs. array_any's short-circuit on the
 * *first truthy*). The architecture is the same
 * (callClosureFromNative with argCount=1); the only new
 * thing is the early-exit return on the first falsy
 * predicate result, plus the empty-array case returns
 * true (vacuously true: "all of zero things are true"
 * — matches JS Array.prototype.every and Python's all).
 *
 * Stack layout for callClosure: [callee, arg1, ...]. */
static Value arrayAllNative(int argCount, Value *args) {
    if (argCount != 2) {
        runtimeError("array_all() takes 2 arguments (%d given).", argCount);
        return BOOL_VAL(false);
    }
    if (!IS_ARRAY(args[0])) {
        runtimeError("array_all() argument 0 must be an array.");
        return BOOL_VAL(false);
    }
    if (!IS_CLOSURE(args[1])) {
        runtimeError("array_all() argument 1 must be a function.");
        return BOOL_VAL(false);
    }
    ObjArray *src = AS_ARRAY(args[0]);
    ObjClosure *predicate = AS_CLOSURE(args[1]);
    if (predicate->function->arity != 1) {
        runtimeError("array_all() predicate must take 1 argument (got %d).",
                     predicate->function->arity);
        return BOOL_VAL(false);
    }

    for (int i = 0; i < src->count; i++) {
        /* Same stack layout as array_map/array_any: push
         * predicate (callee) then element (arg 1). */
        push(OBJ_VAL(predicate));  /* callee */
        push(src->elements[i]);    /* arg 1 */
        Value result = callClosureFromNative(predicate, 1);
        /* Short-circuit on the *first falsy*: as soon as
         * the predicate returns a falsy value, return
         * false without iterating the rest. clox's truthy
         * rule: only false and nil are falsy (not 0, not ""). */
        if ((IS_BOOL(result) && !AS_BOOL(result)) || IS_NIL(result)) {
            return BOOL_VAL(false);
        }
    }

    /* No falsy predicate result found: return true.
     * (Also covers the empty-array case, where the loop
     * never executes.) */
    return BOOL_VAL(true);
}

/* Stage 35: array_find(arr, predicate) -> value.
 * Returns the *first element* for which the predicate is
 * truthy, or nil if no element matches (or if the array
 * is empty). The natural next native in the
 * "iterate-and-return-something" pattern after Stage 34
 * (array_all, which returned a bool). The new wrinkle:
 * the result is a Value (an element of the array), not a
 * bool, and the "not found" case returns nil — a special
 * sentinel that means "no match." This is the first
 * native that returns a Value that is not a bool, an
 * array, or a number.
 *
 * JS reference: Array.prototype.find(predicate).
 * Python reference: next(item for item in arr if
 *   pred(item)), which raises StopIteration if no match;
 *   clox returns nil instead, which is more ergonomic for
 *   the "find-or-default" idiom.
 *
 * Stack layout for callClosure: [callee, arg1, ...]. */
static Value arrayFindNative(int argCount, Value *args) {
    if (argCount != 2) {
        runtimeError("array_find() takes 2 arguments (%d given).", argCount);
        return NIL_VAL;
    }
    if (!IS_ARRAY(args[0])) {
        runtimeError("array_find() argument 0 must be an array.");
        return NIL_VAL;
    }
    if (!IS_CLOSURE(args[1])) {
        runtimeError("array_find() argument 1 must be a function.");
        return NIL_VAL;
    }
    ObjArray *src = AS_ARRAY(args[0]);
    ObjClosure *predicate = AS_CLOSURE(args[1]);
    if (predicate->function->arity != 1) {
        runtimeError("array_find() predicate must take 1 argument (got %d).",
                     predicate->function->arity);
        return NIL_VAL;
    }

    for (int i = 0; i < src->count; i++) {
        /* Same stack layout as array_map/array_any/array_all:
         * push predicate (callee) then element (arg 1). */
        push(OBJ_VAL(predicate));  /* callee */
        push(src->elements[i]);    /* arg 1 */
        Value result = callClosureFromNative(predicate, 1);
        /* Short-circuit: as soon as the predicate returns a
         * truthy value, return that *element* (not the
         * result, not a constant bool). clox's truthy rule:
         * only false and nil are falsy (not 0, not ""). */
        if (!(IS_BOOL(result) && !AS_BOOL(result)) && !IS_NIL(result)) {
            return src->elements[i];
        }
    }

    /* No match found: return nil. This is the "sentinel"
     * pattern — nil is the canonical "no match" value in
     * clox (no Optional, no error). Caller can use
     * 'find(x) == nil' to test for "no match." */
    return NIL_VAL;
}

/* Stage 36: array_find_index(arr, predicate) -> number.
 * Returns the *index* of the first element for which the
 * predicate is truthy, or -1 if no element matches (or
 * if the array is empty). The natural next native after
 * Stage 35 (array_find, which returned the element).
 * The new wrinkle: the result is a number (the index, an
 * int), and the "not found" case returns -1 — the
 * canonical "no match" sentinel for index-based searches
 * in JS, Python, and C.
 *
 * JS reference: Array.prototype.findIndex(predicate).
 * Python reference: list.index(item), which raises
 *   ValueError if not found; clox returns -1 instead,
 *   which is more ergonomic for the "is this in the
 *   array, and at what index" idiom.
 *
 * Stack layout for callClosure: [callee, arg1, ...]. */
static Value arrayFindIndexNative(int argCount, Value *args) {
    if (argCount != 2) {
        runtimeError("array_find_index() takes 2 arguments (%d given).", argCount);
        return NUMBER_VAL(-1);
    }
    if (!IS_ARRAY(args[0])) {
        runtimeError("array_find_index() argument 0 must be an array.");
        return NUMBER_VAL(-1);
    }
    if (!IS_CLOSURE(args[1])) {
        runtimeError("array_find_index() argument 1 must be a function.");
        return NUMBER_VAL(-1);
    }
    ObjArray *src = AS_ARRAY(args[0]);
    ObjClosure *predicate = AS_CLOSURE(args[1]);
    if (predicate->function->arity != 1) {
        runtimeError("array_find_index() predicate must take 1 argument (got %d).",
                     predicate->function->arity);
        return NUMBER_VAL(-1);
    }

    for (int i = 0; i < src->count; i++) {
        /* Same stack layout as array_map/array_any/array_all/
         * array_find: push predicate (callee) then element
         * (arg 1). */
        push(OBJ_VAL(predicate));  /* callee */
        push(src->elements[i]);    /* arg 1 */
        Value result = callClosureFromNative(predicate, 1);
        /* Short-circuit: as soon as the predicate returns a
         * truthy value, return the *index* (not the
         * element, not a constant bool). clox's truthy
         * rule: only false and nil are falsy (not 0, not ""). */
        if (!(IS_BOOL(result) && !AS_BOOL(result)) && !IS_NIL(result)) {
            return NUMBER_VAL((double)i);
        }
    }

    /* No match found: return -1. This is the "sentinel"
     * pattern for index-based searches. Caller can use
     * 'find_index(x) == -1' to test for "no match." */
    return NUMBER_VAL(-1);
}

/* Stage 37: array_zip(arr1, arr2, combiner) -> array.
 * Combines two arrays element-wise via a 2-arg Lox
 * closure (the combiner: (a, b) -> result). Returns a
 * new array where each element is combiner(arr1[i],
 * arr2[i]). Truncates to the shorter of the two arrays.
 * The natural next native after Stage 36 (array_find_index).
 * The new wrinkle: TWO source arrays (not one); the
 * combiner is 2-arg (the 2-arg closure path was verified
 * in Stage 32 with array_reduce).
 *
 * JS reference: there is no direct equivalent, but
 *   lodash's _.zip([arr1, arr2]) does the same.
 * Python reference: zip(arr1, arr2) — truncates to the
 *   shorter; clox matches this convention.
 * Rust reference: Iterator::zip — also truncates to the
 *   shorter.
 *
 * Stack layout for callClosure: [callee, arg1, arg2, ...].
 * For 2-arg, that's 3 items on the stack. */
static Value arrayZipNative(int argCount, Value *args) {
    if (argCount != 3) {
        runtimeError("array_zip() takes 3 arguments (%d given).", argCount);
        return NIL_VAL;  /* error sentinel; matches array_filter / array_reduce */
    }
    if (!IS_ARRAY(args[0])) {
        runtimeError("array_zip() argument 0 must be an array.");
        return NIL_VAL;
    }
    if (!IS_ARRAY(args[1])) {
        runtimeError("array_zip() argument 1 must be an array.");
        return NIL_VAL;
    }
    if (!IS_CLOSURE(args[2])) {
        runtimeError("array_zip() argument 2 must be a function.");
        return NIL_VAL;
    }
    ObjArray *arr1 = AS_ARRAY(args[0]);
    ObjArray *arr2 = AS_ARRAY(args[1]);
    ObjClosure *combiner = AS_CLOSURE(args[2]);
    if (combiner->function->arity != 2) {
        runtimeError("array_zip() combiner must take 2 arguments (got %d).",
                     combiner->function->arity);
        return NIL_VAL;
    }

    /* GC protection: build the result array BEFORE calling
     * the combiner, so the combiner's allocations don't
     * sweep away our partial result. */
    int minCount = arr1->count < arr2->count ? arr1->count : arr2->count;
    ObjArray *result = newArray(minCount);
    push(OBJ_VAL(result));  /* GC: keep alive while filling */

    for (int i = 0; i < minCount; i++) {
        /* Stack layout for callClosure with argCount=2:
         * [callee, arg1, arg2]. The combiner's bytecode
         * reads its args at slots[1] and slots[2].
         *
         * Stack before: [..., arr1, arr2, combiner, result_array]
         * Stack after:  [..., arr1, arr2, combiner, result_array, combiner, arg1, arg2]
         *
         * callClosure(combiner, 2) sets frame->slots to the
         * combiner position (stackTop - 3), so slots[0] =
         * combiner, slots[1] = arg1, slots[2] = arg2. The
         * combiner's bytecode accesses its args at slots[1]
         * and slots[2]. */
        push(OBJ_VAL(combiner));   /* callee */
        push(arr1->elements[i]);   /* arg 1 */
        push(arr2->elements[i]);   /* arg 2 */
        Value combined = callClosureFromNative(combiner, 2);
        /* Push the combiner's return value to the result.
         * The GC is safe: the result array is on the stack
         * below the native's frames, and any allocations
         * the combiner made are handled by its own frame's
         * GC checkpoints. The callee + 2 args are part of
         * the callClosure's frame; we don't manually pop
         * them. */
        arrayPush(result, combined);
    }

    pop();  /* pop the result array (it stays on the stack until
             * the function returns; the VM will handle pushing
             * the return value) */
    return OBJ_VAL(result);
}

static Value arrayZipLongestNative(int argCount, Value *args) {
    if (argCount < 2 || argCount > 3) {
        runtimeError("array_zip_longest() takes 2 or 3 arguments (%d given).", argCount);
        return NIL_VAL;  /* error sentinel */
    }
    if (!IS_ARRAY(args[0])) {
        runtimeError("array_zip_longest() argument 0 must be an array.");
        return NIL_VAL;
    }
    if (!IS_ARRAY(args[1])) {
        runtimeError("array_zip_longest() argument 1 must be an array.");
        return NIL_VAL;
    }
    ObjArray *arr1 = AS_ARRAY(args[0]);
    ObjArray *arr2 = AS_ARRAY(args[1]);
    /* Optional 3rd arg: the fill value. If absent, default to nil.
     * The new wrinkle: first time a clox native has an optional
     * positional parameter. clox's native API uses (int argCount,
     * Value *args) — the optional pattern is "if (argCount > 2)
     * use args[2] else use NIL_VAL". */
    Value fill = (argCount == 3) ? args[2] : NIL_VAL;
    int maxCount = arr1->count > arr2->count ? arr1->count : arr2->count;
    ObjArray *result = newArray(maxCount);
    push(OBJ_VAL(result));  /* GC: keep alive while filling */
    for (int i = 0; i < maxCount; i++) {
        ObjArray *pair = newArray(2);
        push(OBJ_VAL(pair));  /* GC: keep pair alive */
        if (i < arr1->count) {
            arrayPush(pair, arr1->elements[i]);
        } else {
            arrayPush(pair, fill);
        }
        if (i < arr2->count) {
            arrayPush(pair, arr2->elements[i]);
        } else {
            arrayPush(pair, fill);
        }
        pop();  /* pop the pair */
        arrayPush(result, OBJ_VAL(pair));
    }
    pop();  /* pop the result array */
    return OBJ_VAL(result);
}

static Value arrayTakeNative(int argCount, Value *args) {
    if (argCount != 2) {
        runtimeError("array_take() takes 2 arguments (%d given).", argCount);
        return NIL_VAL;  /* error sentinel */
    }
    if (!IS_ARRAY(args[0])) {
        runtimeError("array_take() argument 0 must be an array.");
        return NIL_VAL;
    }
    if (!IS_NUMBER(args[1])) {
        runtimeError("array_take() argument 1 must be a number.");
        return NIL_VAL;
    }
    ObjArray *arr = AS_ARRAY(args[0]);
    int n = (int)AS_NUMBER(args[1]);
    /* The new wrinkle: a "size limit" pattern, similar
     * to Stage 41's array_chunk. If N <= 0, return an
     * empty array. If N >= array length, return the
     * full array. Otherwise return the first N
     * elements. */
    if (n <= 0) {
        ObjArray *empty = newArray(0);
        return OBJ_VAL(empty);
    }
    int takeCount = n < arr->count ? n : arr->count;
    ObjArray *result = newArray(takeCount);
    push(OBJ_VAL(result));  /* GC: keep alive while filling */
    for (int i = 0; i < takeCount; i++) {
        arrayPush(result, arr->elements[i]);
    }
    pop();  /* pop the result array */
    return OBJ_VAL(result);
}

static Value arrayDropNative(int argCount, Value *args) {
    if (argCount != 2) {
        runtimeError("array_drop() takes 2 arguments (%d given).", argCount);
        return NIL_VAL;  /* error sentinel */
    }
    if (!IS_ARRAY(args[0])) {
        runtimeError("array_drop() argument 0 must be an array.");
        return NIL_VAL;
    }
    if (!IS_NUMBER(args[1])) {
        runtimeError("array_drop() argument 1 must be a number.");
        return NIL_VAL;
    }
    ObjArray *arr = AS_ARRAY(args[0]);
    int n = (int)AS_NUMBER(args[1]);
    /* The new wrinkle: the "skip the first N"
     * pattern, the complement of Stage 51's
     * array_take. If N <= 0, return the full
     * array. If N >= array length, return an
     * empty array. Otherwise return the elements
     * from index N to the end. */
    if (n <= 0) {
        ObjArray *full = newArray(arr->count);
        push(OBJ_VAL(full));  /* GC: keep alive while filling */
        for (int i = 0; i < arr->count; i++) {
            arrayPush(full, arr->elements[i]);
        }
        pop();  /* pop the full array */
        return OBJ_VAL(full);
    }
    if (n >= arr->count) {
        ObjArray *empty = newArray(0);
        return OBJ_VAL(empty);
    }
    int keepCount = arr->count - n;
    ObjArray *result = newArray(keepCount);
    push(OBJ_VAL(result));  /* GC: keep alive while filling */
    for (int i = n; i < arr->count; i++) {
        arrayPush(result, arr->elements[i]);
    }
    pop();  /* pop the result array */
    return OBJ_VAL(result);
}

/* Stage 38: array_flatten(arr) -> array.
 * Takes an array of arrays and returns a new flat array.
 * Stops at 1 level: inner arrays' elements become
 * top-level, but elements that are themselves arrays
 * are pushed as-is (not recursed into). This is the
 * "shape transform" pattern (nested -> flat) — the
 * simplest remaining candidate. No user-code dispatch
 * (no closure); no new architecture; just a single
 * nested-element walk.
 *
 * JS reference: there is no direct equivalent; lodash's
 *   _.flatten (not _.flattenDeep) does the same.
 * Python reference: no direct equivalent; Python uses
 *   itertools.chain.from_iterable(arr) for 1-level, or
 *   list comprehensions. clox's array_flatten matches
 *   lodash's _.flatten (1-level, stop on nested arrays).
 * Rust reference: no direct equivalent; the idiomatic
 *   version is .flatten() on iterators, which does
 *   recurse (clox does NOT recurse).
 *
 * The "stop at 1 level" convention is the right default
 * for a stdlib native: predictable, easy to reason
 * about, and easy to compose. If the caller wants
 * recursive flatten, they can chain: flatten twice
 * (one level per call) or build a recursive user
 * function. The new wrinkle: this is the first native
 * that takes a "nested array" (array of arrays) and
 * returns a flat array. */
static Value arrayFlattenNative(int argCount, Value *args) {
    if (argCount != 1) {
        runtimeError("array_flatten() takes 1 argument (%d given).", argCount);
        return NIL_VAL;  /* error sentinel */
    }
    if (!IS_ARRAY(args[0])) {
        runtimeError("array_flatten() argument must be an array.");
        return NIL_VAL;
    }
    ObjArray *src = AS_ARRAY(args[0]);

    /* First pass: count the result size. We do this so
     * the result array can be allocated with the right
     * capacity up front (no resize). For each element:
     * - if it's an array, count its elements
     * - else, count 1
     * The discipline: pre-size the result for O(1) push
     * per element (no array growth). */
    int totalCount = 0;
    for (int i = 0; i < src->count; i++) {
        if (IS_ARRAY(src->elements[i])) {
            totalCount += AS_ARRAY(src->elements[i])->count;
        } else {
            totalCount += 1;
        }
    }

    /* GC protection: build the result array BEFORE the
     * fill loop, so any allocations during the fill
     * (none expected here, but defensive) don't sweep
     * away our partial result. */
    ObjArray *result = newArray(totalCount);
    push(OBJ_VAL(result));  /* GC: keep alive while filling */

    /* Second pass: fill the result. For each element:
     * - if it's an array, push its elements (1 level)
     * - else, push the element as-is */
    for (int i = 0; i < src->count; i++) {
        if (IS_ARRAY(src->elements[i])) {
            ObjArray *inner = AS_ARRAY(src->elements[i]);
            for (int j = 0; j < inner->count; j++) {
                arrayPush(result, inner->elements[j]);
            }
        } else {
            arrayPush(result, src->elements[i]);
        }
    }

    pop();  /* pop the result array (it stays on the stack until
             * the function returns; the VM will handle pushing
             * the return value) */
    return OBJ_VAL(result);
}

/* Stage 41: array_chunk(arr, size) -> array.
 * Chunks an array into fixed-size sub-arrays. The
 * shape: 2 args (array, size). The size is the chunk
 * size (must be > 0). The result is an array of
 * arrays; the last chunk may be shorter if the input
 * length isn't a multiple of size. Empty input returns
 * an empty array (no chunks). size > length returns
 * one chunk (the input as a single sub-array).
 * size <= 0 errors. size must be a number.
 *
 * No user-code dispatch (no closure). Architecture
 * reuses Stage 12 (arrays as first-class heap values)
 * and Stage 38's pre-count + pre-allocate pattern.
 *
 * The new wrinkle: this is the first native that
 * returns "array of arrays" (a 2D structure). The
 * caller may need to flatten the result to iterate
 * the elements, or iterate the chunks themselves.
 *
 * Edge cases:
 * - array_chunk([], n) -> [] (empty array of chunks)
 * - array_chunk([1], 1) -> [[1]] (one chunk of size 1)
 * - array_chunk([1,2,3], 4) -> [[1,2,3]] (one chunk, shorter than size)
 * - array_chunk([1,2,3,4], 0) -> ERROR (size must be > 0)
 * - array_chunk([1,2,3,4], -1) -> ERROR (size must be > 0)
 */
static Value arrayChunkNative(int argCount, Value *args) {
    if (argCount != 2) {
        runtimeError("array_chunk() takes 2 arguments (%d given).", argCount);
        return NIL_VAL;
    }
    if (!IS_ARRAY(args[0])) {
        runtimeError("array_chunk() argument must be an array.");
        return NIL_VAL;
    }
    if (!IS_NUMBER(args[1])) {
        runtimeError("array_chunk() size must be a number.");
        return NIL_VAL;
    }
    int size = (int)AS_NUMBER(args[1]);
    if (size <= 0) {
        runtimeError("array_chunk() size must be > 0 (got %d).", size);
        return NIL_VAL;
    }
    ObjArray *src = AS_ARRAY(args[0]);
    /* Pre-count: ceil(count / size) chunks. */
    int numChunks = (src->count + size - 1) / size;
    if (src->count == 0) {
        numChunks = 0;  /* empty input -> no chunks */
    }
    ObjArray *result = newArray(numChunks);
    push(OBJ_VAL(result));  /* GC: keep alive while filling */
    for (int i = 0; i < numChunks; i++) {
        int chunkStart = i * size;
        int chunkEnd = chunkStart + size;
        if (chunkEnd > src->count) {
            chunkEnd = src->count;  /* last chunk may be shorter */
        }
        ObjArray *chunk = newArray(chunkEnd - chunkStart);
        /* GC: push the chunk before filling. The
         * outer result is already on the stack (above),
         * so the chunk is doubly protected. */
        push(OBJ_VAL(chunk));
        for (int j = chunkStart; j < chunkEnd; j++) {
            arrayPush(chunk, src->elements[j]);
        }
        pop();  /* chunk */
        arrayPush(result, OBJ_VAL(chunk));
    }
    pop();  /* result */
    return OBJ_VAL(result);
}

/* Stage 42: array_group_by(arr, keyFn) -> array.
 * Groups elements of an array by a key function. The
 * shape: 2 args (array, keyFn). The keyFn is a 1-arg
 * Lox closure: keyFn(element) -> key. Returns an
 * array of arrays (the groups), in first-occurrence-
 * of-each-key order. Elements with the same key go
 * into the same group. The key itself is not included
 * in the output (matches lodash's _.groupBy, which
 * returns the groups only). Empty array returns
 * empty array. No user-code dispatch beyond the 1-arg
 * closure path (reuses Stage 30 unchanged).
 *
 * The new wrinkle: this is the first native that
 * maintains a parallel "keys so far" array AND a
 * parallel "groups so far" array, where the index
 * in the keys array determines the index in the
 * groups array. The keys array is a unique-key
 * registry; the groups array is the result. Each
 * element of the input is appended to its group's
 * chunk.
 *
 * JS reference: lodash's _.groupBy(collection, iteratee)
 * Python reference: more-itertools's map_reduce(iterable, keyfn)
 * Rust reference: itertools's group_by
 * The canonical convention across all three: groups only,
 * in first-occurrence order, no key duplication in the
 * output.
 */
static Value arrayGroupByNative(int argCount, Value *args) {
    if (argCount != 2) {
        runtimeError("array_group_by() takes 2 arguments (%d given).", argCount);
        return NIL_VAL;
    }
    if (!IS_ARRAY(args[0])) {
        runtimeError("array_group_by() argument must be an array.");
        return NIL_VAL;
    }
    if (!IS_CLOSURE(args[1])) {
        runtimeError("array_group_by() keyFn must be a function.");
        return NIL_VAL;
    }
    ObjArray *input = AS_ARRAY(args[0]);
    ObjClosure *keyFn = AS_CLOSURE(args[1]);
    /* Two parallel Lox-heap arrays: seenKeys[i] is the
     * i-th unique key, result[i] is the i-th group (a
     * sub-array of input elements that share that key).
     * Both grow dynamically. */
    ObjArray *seenKeys = newArray(0);
    push(OBJ_VAL(seenKeys));  /* GC: keep alive while filling */
    ObjArray *result = newArray(0);
    push(OBJ_VAL(result));  /* GC: keep alive while filling */
    for (int i = 0; i < input->count; i++) {
        Value candidate = input->elements[i];
        /* Compute the key by calling keyFn(candidate). The
         * stack layout for callClosureFromNative is
         * [callee, arg1, ..., argN]; push them in order. */
        push(OBJ_VAL(keyFn));
        push(candidate);
        Value key = callClosureFromNative(keyFn, 1);
        /* Scan the seen keys to find this key's index. */
        int groupIndex = -1;
        for (int j = 0; j < seenKeys->count; j++) {
            if (valuesEqual(key, seenKeys->elements[j])) {
                groupIndex = j;
                break;
            }
        }
        if (groupIndex == -1) {
            /* New key: append to seenKeys, create a new group. */
            arrayPush(seenKeys, key);
            ObjArray *newGroup = newArray(1);
            /* GC: push the new group before pushing the
             * element. The group needs to be alive while we
             * push the element, and the result needs to be
             * alive while we push the group. */
            push(OBJ_VAL(newGroup));
            arrayPush(newGroup, candidate);
            pop();  /* newGroup */
            arrayPush(result, OBJ_VAL(newGroup));
        } else {
            /* Existing key: append to the existing group. */
            ObjArray *existingGroup = AS_ARRAY(result->elements[groupIndex]);
            arrayPush(existingGroup, candidate);
        }
    }
    pop();  /* result */
    pop();  /* seenKeys */
    return OBJ_VAL(result);
}

/* Stage 43: array_sort(arr, comparator?) -> array.
 * Sorts an array, returning a new sorted array. The
 * input is not mutated. The shape: 1 or 2 args.
 * - 1 arg: default < for numbers, lexicographic for
 *   strings. Mixed types error at the first comparison.
 * - 2 args, keyFn: sorts by keyFn(element) (1-arg
 *   Lox closure). The comparator is < on keys.
 * - 2 args, comparator: sorts using a 2-arg
 *   comparator closure. Returns negative if a < b,
 *   0 if equal, positive if a > b.
 *
 * Stable sort: insertion sort. O(N^2) worst case but
 * simple, stable, and correct. For very large arrays,
 * a future stage could add a quicksort/mergesort/
 * timsort variant. The discipline: **stable is the
 * right default** — preserves first-occurrence order
 * for equal elements, matches the "first-occurrence"
 * convention used in Stage 40 (array_unique_by) and
 * Stage 42 (array_group_by).
 *
 * Architecture: reuses Stage 30's callClosureFromNative
 * (argCount=1 for keyFn, argCount=2 for comparator).
 * Stack layout for closure call: [callee, arg1, ..., argN].
 * Returns Value from the comparator (negative = a < b,
 * 0 = equal, positive = a > b).
 *
 * JS reference: Array.prototype.sort(comparator) (in-place;
 * clox returns a new array to match the non-mutating pattern
 * of Stages 38, 40, 41, 42)
 * Python reference: sorted(iterable, key=fn, reverse=bool)
 * Rust reference: slice::sort_by(cmp)
 * The canonical convention: stable sort, comparator
 * returns negative/zero/positive (not boolean).
 */
/* Forward declaration: sortCompareValues is defined
 * below arraySortNative but called from inside it. */
static int sortCompareValues(Value a, Value b,
                             bool hasKeyFn, ObjClosure *keyFn,
                             bool hasComparator, ObjClosure *comparator);
static Value arraySortNative(int argCount, Value *args) {
    if (argCount < 1 || argCount > 2) {
        runtimeError("array_sort() takes 1 or 2 arguments (%d given).", argCount);
        return NIL_VAL;
    }
    if (!IS_ARRAY(args[0])) {
        runtimeError("array_sort() argument must be an array.");
        return NIL_VAL;
    }
    /* If 2 args, the second must be a closure (keyFn or comparator). */
    bool hasKeyFn = false;
    bool hasComparator = false;
    ObjClosure *keyFn = NULL;
    ObjClosure *comparator = NULL;
    if (argCount == 2) {
        if (!IS_CLOSURE(args[1])) {
            runtimeError("array_sort() second argument must be a function.");
            return NIL_VAL;
        }
        /* Disambiguate keyFn (1-arg) vs comparator (2-arg) by arity
         * of the closure. clox closures have arity stored in the
         * function object. */
        int arity = AS_CLOSURE(args[1])->function->arity;
        if (arity == 1) {
            hasKeyFn = true;
            keyFn = AS_CLOSURE(args[1]);
        } else if (arity == 2) {
            hasComparator = true;
            comparator = AS_CLOSURE(args[1]);
        } else {
            runtimeError("array_sort() keyFn/comparator must take 1 or 2 arguments (got %d).", arity);
            return NIL_VAL;
        }
    }
    ObjArray *input = AS_ARRAY(args[0]);
    /* Allocate the result array with the same capacity. */
    ObjArray *result = newArray(input->count);
    push(OBJ_VAL(result));  /* GC: keep alive while filling and sorting */
    for (int i = 0; i < input->count; i++) {
        arrayPush(result, input->elements[i]);
    }
    /* Insertion sort: for each element from index 1 to N-1,
     * shift it left until it's in the right position. Stable
     * because we use strict < (not <=) — equal elements
     * don't swap. */
    for (int i = 1; i < result->count; i++) {
        Value current = result->elements[i];
        int j = i;
        while (j > 0) {
            Value prev = result->elements[j - 1];
            if (sortCompareValues(prev, current, hasKeyFn, keyFn, hasComparator, comparator) <= 0) {
                break;
            }
            result->elements[j] = prev;
            j--;
        }
        result->elements[j] = current;
    }
    pop();  /* result */
    return OBJ_VAL(result);
}

static int sortCompareValues(Value a, Value b,
                             bool hasKeyFn, ObjClosure *keyFn,
                             bool hasComparator, ObjClosure *comparator) {
    if (hasComparator) {
        /* Call comparator(a, b) and return the result as int. */
        push(OBJ_VAL(comparator));
        push(a);
        push(b);
        Value cmp = callClosureFromNative(comparator, 2);
        if (!IS_NUMBER(cmp)) {
            runtimeError("array_sort() comparator must return a number.");
            return 0;
        }
        return (int)AS_NUMBER(cmp);
    }
    if (hasKeyFn) {
        /* Compute keyA and keyB, then compare with default <. */
        push(OBJ_VAL(keyFn));
        push(a);
        Value keyA = callClosureFromNative(keyFn, 1);
        push(OBJ_VAL(keyFn));
        push(b);
        Value keyB = callClosureFromNative(keyFn, 1);
        a = keyA;
        b = keyB;
    }
    /* Default <: numbers use <, strings use lexicographic
     * (compare character by character). */
    if (IS_NUMBER(a) && IS_NUMBER(b)) {
        double na = AS_NUMBER(a);
        double nb = AS_NUMBER(b);
        if (na < nb) return -1;
        if (na > nb) return 1;
        return 0;
    }
    if (IS_STRING(a) && IS_STRING(b)) {
        ObjString *sa = AS_STRING(a);
        ObjString *sb = AS_STRING(b);
        int minLen = sa->length < sb->length ? sa->length : sb->length;
        for (int i = 0; i < minLen; i++) {
            if (sa->chars[i] < sb->chars[i]) return -1;
            if (sa->chars[i] > sb->chars[i]) return 1;
        }
        if (sa->length < sb->length) return -1;
        if (sa->length > sb->length) return 1;
        return 0;
    }
    runtimeError("array_sort() cannot compare values of different types.");
    return 0;
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
            /* Stage 39: added OBJ_ARRAY case. Before this
             * patch, typeof(<array>) returned "object" (the
             * default), which was a latent issue surfaced by
             * Stage 38's test-bug #2. After this patch,
             * typeof([1, 2, 3]) returns "array" (matches the
             * mental model and the JS-ish convention). */
            case OBJ_ARRAY:        name = "array";   break;
            default:               name = "object";   break;
        }
    } else {
        name = "unknown";
    }
    return OBJ_VAL(copyString(name, (int)strlen(name)));
}

static Value isArrayNative(int argCount, Value *args) {
    if (argCount != 1) {
        runtimeError("is_array() takes 1 argument (%d given).", argCount);
        return NIL_VAL;
    }
    /* The "type predicate" pattern. Inspect the
     * Value's type tag directly. For clox, an array
     * is represented as an Obj with OBJ_ARRAY type.
     * IS_OBJ/OBJ_TYPE are the standard macros (used
     * by typeofNative for the same purpose). The
     * alternative is a separate "is_array" VM opcode,
     * but a native keeps the VM unchanged and is
     * consistent with typeof(). Returns a bool
     * (BOOL_VAL, not OBJ_VAL). */
    if (IS_OBJ(args[0]) && OBJ_TYPE(args[0]) == OBJ_ARRAY) {
        return BOOL_VAL(true);
    }
    return BOOL_VAL(false);
}

static Value isStringNative(int argCount, Value *args) {
    if (argCount != 1) {
        runtimeError("is_string() takes 1 argument (%d given).", argCount);
        return NIL_VAL;
    }
    /* The "type predicate" pattern (Stage 45, after
     * Stage 44's is_array). Inspect the Value's type
     * tag directly. For clox, a string is represented
     * as an Obj with OBJ_STRING type. The alternative
     * is a separate "is_string" VM opcode, but a
     * native keeps the VM unchanged and is consistent
     * with typeof() and is_array(). Returns a bool
     * (BOOL_VAL, not OBJ_VAL). */
    if (IS_OBJ(args[0]) && OBJ_TYPE(args[0]) == OBJ_STRING) {
        return BOOL_VAL(true);
    }
    return BOOL_VAL(false);
}

static Value isNumberNative(int argCount, Value *args) {
    if (argCount != 1) {
        runtimeError("is_number() takes 1 argument (%d given).", argCount);
        return NIL_VAL;
    }
    /* The "type predicate" pattern (Stage 46, after
     * Stage 44's is_array and Stage 45's is_string).
     * Inspect the Value's type tag directly. For
     * clox, a number is represented as a double
     * inside a Value (not an Obj). The alternative
     * is a separate "is_number" VM opcode, but a
     * native keeps the VM unchanged and is consistent
     * with typeof() and is_array() and is_string().
     * Returns a bool (BOOL_VAL, not OBJ_VAL). */
    if (IS_NUMBER(args[0])) {
        return BOOL_VAL(true);
    }
    return BOOL_VAL(false);
}

static Value isBoolNative(int argCount, Value *args) {
    if (argCount != 1) {
        runtimeError("is_bool() takes 1 argument (%d given).", argCount);
        return NIL_VAL;
    }
    /* The "type predicate" pattern (Stage 47, after
     * Stage 44's is_array, Stage 45's is_string, and
     * Stage 46's is_number). Inspect the Value's
     * type tag directly. For clox, a bool is a
     * dedicated Value tag (VAL_BOOL). The alternative
     * is a separate "is_bool" VM opcode, but a native
     * keeps the VM unchanged and is consistent with
     * typeof() and is_array() and is_string() and
     * is_number(). Returns a bool (BOOL_VAL, not
     * OBJ_VAL). */
    if (IS_BOOL(args[0])) {
        return BOOL_VAL(true);
    }
    return BOOL_VAL(false);
}

static Value isNilNative(int argCount, Value *args) {
    if (argCount != 1) {
        runtimeError("is_nil() takes 1 argument (%d given).", argCount);
        return NIL_VAL;
    }
    /* The "type predicate" pattern (Stage 48, after
     * Stage 44's is_array, Stage 45's is_string,
     * Stage 46's is_number, and Stage 47's is_bool).
     * Inspect the Value's type tag directly. For
     * clox, nil is a dedicated Value tag (VAL_NIL).
     * The alternative is a separate "is_nil" VM
     * opcode, but a native keeps the VM unchanged
     * and is consistent with typeof() and is_array()
     * and is_string() and is_number() and is_bool().
     * Returns a bool (BOOL_VAL, not OBJ_VAL). */
    if (IS_NIL(args[0])) {
        return BOOL_VAL(true);
    }
    return BOOL_VAL(false);
}

static Value isFunctionNative(int argCount, Value *args) {
    if (argCount != 1) {
        runtimeError("is_function() takes 1 argument (%d given).", argCount);
        return NIL_VAL;
    }
    /* The "type predicate" pattern (Stage 49, after
     * Stage 44's is_array, Stage 45's is_string,
     * Stage 46's is_number, Stage 47's is_bool, and
     * Stage 48's is_nil). The 6th and final type
     * predicate.
     *
     * The new wrinkle: function-ness in clox has
     * TWO sub-types: ObjClosure (Lox-defined) and
     * ObjNative (C-defined). is_function returns
     * true for both — matches the JS/Python mental
     * model where `typeof x == "function"` returns
     * true for both Lox closures and built-ins.
     * The alternative is to have separate
     * is_closure() and is_native() predicates, but
     * that's a finer-grained split than callers
     * usually want.
     *
     * For clox, both ObjClosure and ObjNative are
     * Obj with specific OBJ_TYPE values (OBJ_CLOSURE
     * and OBJ_NATIVE). The check is:
     * IS_OBJ(args[0]) && (OBJ_TYPE(args[0]) ==
     * OBJ_CLOSURE || OBJ_TYPE(args[0]) ==
     * OBJ_NATIVE). */
    if (IS_OBJ(args[0])) {
        ObjType type = OBJ_TYPE(args[0]);
        if (type == OBJ_CLOSURE || type == OBJ_NATIVE) {
            return BOOL_VAL(true);
        }
    }
    return BOOL_VAL(false);
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

    /* Stage 17: number-to-string conversion. */
    name = copyString("string", (int)strlen("string"));
    push(OBJ_VAL(name));
    tableSet(&vm.globals, name, OBJ_VAL(newNative(stringFromNumberNative)));
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

    /* Stage 27: string_trim_start / string_trim_end. Two new
     * natives, mirror of Stage 9's string_trim but only one
     * side. */
    name = copyString("string_trim_start", (int)strlen("string_trim_start"));
    push(OBJ_VAL(name));
    tableSet(&vm.globals, name, OBJ_VAL(newNative(stringTrimStartNative)));
    pop();

    name = copyString("string_trim_end", (int)strlen("string_trim_end"));
    push(OBJ_VAL(name));
    tableSet(&vm.globals, name, OBJ_VAL(newNative(stringTrimEndNative)));
    pop();

    /* Stage 21: string_repeat. */
    name = copyString("string_repeat", (int)strlen("string_repeat"));
    push(OBJ_VAL(name));
    tableSet(&vm.globals, name, OBJ_VAL(newNative(stringRepeatNative)));
    pop();

    /* Stage 22: string_pad_start. */
    name = copyString("string_pad_start", (int)strlen("string_pad_start"));
    push(OBJ_VAL(name));
    tableSet(&vm.globals, name, OBJ_VAL(newNative(stringPadStartNative)));
    pop();

    /* Stage 23: string_pad_end. */
    name = copyString("string_pad_end", (int)strlen("string_pad_end"));
    push(OBJ_VAL(name));
    tableSet(&vm.globals, name, OBJ_VAL(newNative(stringPadEndNative)));
    pop();

    /* Stage 24: string_to_number. The inverse of Stage 17's string(n). */
    name = copyString("string_to_number", (int)strlen("string_to_number"));
    push(OBJ_VAL(name));
    tableSet(&vm.globals, name, OBJ_VAL(newNative(stringToNumberNative)));
    pop();

    /* Stage 26: string_to_int. The integer-only parse, a NEW
     * native (not an extension of string_to_number). Closes the
     * '42 vs 42.0' question. */
    name = copyString("string_to_int", (int)strlen("string_to_int"));
    push(OBJ_VAL(name));
    tableSet(&vm.globals, name, OBJ_VAL(newNative(stringToIntNative)));
    pop();

    /* Stage 13: string_split / string_join — compose the new array
     * value type with the existing string natives. */
    name = copyString("string_split", (int)strlen("string_split"));
    push(OBJ_VAL(name));
    tableSet(&vm.globals, name, OBJ_VAL(newNative(stringSplitNative)));
    pop();

    name = copyString("string_join", (int)strlen("string_join"));
    push(OBJ_VAL(name));
    tableSet(&vm.globals, name, OBJ_VAL(newNative(stringJoinNative)));
    pop();

    /* Stage 30: array_filter. Takes an array and a 1-arg Lox
     * closure (a callable). Returns a new array containing
     * only the elements for which the predicate returns true.
     * The first native that invokes user-defined Lox code from
     * C; the `call` function (vm.c, now public) does the work. */
    name = copyString("array_filter", (int)strlen("array_filter"));
    push(OBJ_VAL(name));
    tableSet(&vm.globals, name, OBJ_VAL(newNative(arrayFilterNative)));
    pop();

    /* Stage 31: array_map. Takes an array and a 1-arg Lox
     * closure (the transform). Returns a new array where each
     * element is `transform(element)`. The natural mirror of
     * Stage 30's array_filter. Uses the same user-code dispatch
     * architecture (callClosureFromNative + OP_RETURN target
     * check). */
    name = copyString("array_map", (int)strlen("array_map"));
    push(OBJ_VAL(name));
    tableSet(&vm.globals, name, OBJ_VAL(newNative(arrayMapNative)));
    pop();

    /* Stage 32: array_reduce. Takes an array, a 2-arg Lox
     * closure (the reducer: (accumulator, element) -> newAcc),
     * and an initial value. Returns a single value (the final
     * accumulator). The natural mirror of Stage 31's array_map.
     * The new wrinkle: the reducer is 2-arg, not 1-arg. The
     * architecture already supports N-arg closures
     * (callClosureFromNative takes an argCount); Stage 32 just
     * exercises that with argCount=2. */
    name = copyString("array_reduce", (int)strlen("array_reduce"));
    push(OBJ_VAL(name));
    tableSet(&vm.globals, name, OBJ_VAL(newNative(arrayReduceNative)));
    pop();

    /* Stage 33: array_any. Takes an array and a 1-arg Lox
     * closure (the predicate). Returns true as soon as the
     * predicate is truthy on any element (short-circuit),
     * false if all elements are falsy. The natural mirror of
     * Stage 30's array_filter, with the first deviation from
     * the "iterate everything" pattern. The new wrinkle:
     * early-exit return on the first truthy predicate result.
     * Architecture is the same as Stage 30/31 (argCount=1). */
    name = copyString("array_any", (int)strlen("array_any"));
    push(OBJ_VAL(name));
    tableSet(&vm.globals, name, OBJ_VAL(newNative(arrayAnyNative)));
    pop();

    /* Stage 34: array_all. Takes an array and a 1-arg Lox
     * closure (the predicate). Returns false as soon as the
     * predicate is falsy on any element (short-circuit),
     * true if all elements are truthy. The natural mirror of
     * Stage 33's array_any, with the short-circuit direction
     * reversed. The new wrinkle: short-circuit on the *first
     * falsy* (vs. Stage 33's short-circuit on the *first
     * truthy*). The empty-array case returns true (vacuously
     * true: "all of zero things are true" — matches JS
     * Array.prototype.every and Python's all). Architecture
     * is the same as Stage 30/31/33 (argCount=1). */
    name = copyString("array_all", (int)strlen("array_all"));
    push(OBJ_VAL(name));
    tableSet(&vm.globals, name, OBJ_VAL(newNative(arrayAllNative)));
    pop();

    /* Stage 35: array_find. Takes an array and a 1-arg Lox
     * closure (the predicate). Returns the *first element*
     * for which the predicate is truthy, or nil if no
     * element matches (or if the array is empty). The
     * natural next native in the "iterate-and-return-something"
     * pattern after Stage 34. The new wrinkle: the result
     * is a Value (an element of the array), not a bool, and
     * the "not found" case returns nil — a special sentinel
     * that means "no match." This is the first native that
     * returns a Value that is not a bool, an array, or a
     * number. Architecture is the same as Stage 30/31/33/34
     * (argCount=1). */
    name = copyString("array_find", (int)strlen("array_find"));
    push(OBJ_VAL(name));
    tableSet(&vm.globals, name, OBJ_VAL(newNative(arrayFindNative)));
    pop();

    /* Stage 36: array_find_index. Takes an array and a
     * 1-arg Lox closure (the predicate). Returns the
     * *index* of the first element for which the predicate
     * is truthy, or -1 if no element matches (or if the
     * array is empty). The natural next native after
     * Stage 35 (array_find, which returned the element).
     * The new wrinkle: the result is a number (the index,
     * an int), and the "not found" case returns -1 — the
     * canonical "no match" sentinel for index-based
     * searches in JS, Python, and C. Architecture is the
     * same as Stage 30/31/33/34/35 (argCount=1). */
    name = copyString("array_find_index", (int)strlen("array_find_index"));
    push(OBJ_VAL(name));
    tableSet(&vm.globals, name, OBJ_VAL(newNative(arrayFindIndexNative)));
    pop();

    /* Stage 37: array_zip. Takes two arrays and a 2-arg
     * Lox closure (the combiner: (a, b) -> result).
     * Returns a new array where each element is
     * combiner(arr1[i], arr2[i]). Truncates to the
     * shorter of the two arrays. The natural next native
     * after Stage 36 (array_find_index). The new wrinkle:
     * TWO source arrays (not one); the combiner is 2-arg
     * (the 2-arg closure path was verified in Stage 32
     * with array_reduce). Architecture is the same as
     * Stage 30/31/32 (argCount=2). Stack layout for
     * callClosure with argCount=2 is [callee, arg1, arg2]. */
    name = copyString("array_zip", (int)strlen("array_zip"));
    push(OBJ_VAL(name));
    tableSet(&vm.globals, name, OBJ_VAL(newNative(arrayZipNative)));
    pop();

    /* Stage 50: array_zip_longest. Like Stage 37's
     * array_zip but pads the shorter with a default.
     * Takes 2 or 3 arguments (the optional 3rd is the
     * fill value; defaults to nil). ~30 lines, reuses
     * Stage 37's array-building pattern. The new
     * wrinkle: first time a clox native has an optional
     * positional parameter. The push/pop count is
     * balanced: 1 push for the name, 1 pop after
     * tableSet. */
    name = copyString("array_zip_longest", (int)strlen("array_zip_longest"));
    push(OBJ_VAL(name));
    tableSet(&vm.globals, name, OBJ_VAL(newNative(arrayZipLongestNative)));
    pop();

    /* Stage 38: array_flatten. Takes an array (of arrays)
     * and returns a new flat array. Stops at 1 level:
     * inner arrays' elements become top-level, but
     * elements that are themselves arrays are pushed
     * as-is (not recursed into). The "shape transform"
     * pattern (nested -> flat) — the simplest remaining
     * candidate. No user-code dispatch (no closure);
     * no new architecture; just a single nested-
     * element walk. Architecture is the same as Stage
     * 30/31/33/34/35/36/37 (no user-code; just
     * argCount=1 array input). */
    name = copyString("array_flatten", (int)strlen("array_flatten"));
    push(OBJ_VAL(name));
    tableSet(&vm.globals, name, OBJ_VAL(newNative(arrayFlattenNative)));
    pop();

    /* Stage 41: array_chunk. Takes an array and a
     * size; returns an array of arrays (the chunks).
     * The last chunk may be shorter if the input
     * length isn't a multiple of size. No user-code
     * dispatch; reuses Stage 12 (arrays) and Stage 38
     * (pre-count + pre-allocate) unchanged. */
    name = copyString("array_chunk", (int)strlen("array_chunk"));
    push(OBJ_VAL(name));
    tableSet(&vm.globals, name, OBJ_VAL(newNative(arrayChunkNative)));
    pop();

    /* Stage 42: array_group_by. Takes an array and a
     * keyFn closure; returns an array of arrays (the
     * groups). Elements with the same key go into the
     * same group. Groups are in first-occurrence-of-
     * each-key order. The key itself is not in the
     * output (matches lodash's _.groupBy). Reuses
     * Stage 30's callClosureFromNative(argCount=1)
     * unchanged. */
    name = copyString("array_group_by", (int)strlen("array_group_by"));
    push(OBJ_VAL(name));
    tableSet(&vm.globals, name, OBJ_VAL(newNative(arrayGroupByNative)));
    pop();

    /* Stage 43: array_sort. Takes an array and an
     * optional keyFn (1-arg) or comparator (2-arg)
     * closure; returns a new sorted array. Default
     * < for numbers, lexicographic for strings. Stable
     * sort (insertion sort) preserves first-occurrence
     * order for equal elements. */
    name = copyString("array_sort", (int)strlen("array_sort"));
    push(OBJ_VAL(name));
    tableSet(&vm.globals, name, OBJ_VAL(newNative(arraySortNative)));
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

    /* Stage 11: I/O natives. */
    name = copyString("io_print", (int)strlen("io_print"));
    push(OBJ_VAL(name));
    tableSet(&vm.globals, name, OBJ_VAL(newNative(ioPrintNative)));
    pop();

    name = copyString("io_eprint", (int)strlen("io_eprint"));
    push(OBJ_VAL(name));
    tableSet(&vm.globals, name, OBJ_VAL(newNative(ioEprintNative)));
    pop();

    name = copyString("io_exit", (int)strlen("io_exit"));
    push(OBJ_VAL(name));
    tableSet(&vm.globals, name, OBJ_VAL(newNative(ioExitNative)));
    pop();

    name = copyString("io_read_line", (int)strlen("io_read_line"));
    push(OBJ_VAL(name));
    tableSet(&vm.globals, name, OBJ_VAL(newNative(ioReadLineNative)));
    pop();

    /* Stage 14: more file I/O natives. */
    name = copyString("io_read_file", (int)strlen("io_read_file"));
    push(OBJ_VAL(name));
    tableSet(&vm.globals, name, OBJ_VAL(newNative(ioReadFileNative)));
    pop();

    name = copyString("io_write_file", (int)strlen("io_write_file"));
    push(OBJ_VAL(name));
    tableSet(&vm.globals, name, OBJ_VAL(newNative(ioWriteFileNative)));
    pop();

    name = copyString("io_file_exists", (int)strlen("io_file_exists"));
    push(OBJ_VAL(name));
    tableSet(&vm.globals, name, OBJ_VAL(newNative(ioFileExistsNative)));
    pop();

    /* Stage 16: streaming I/O. */
    name = copyString("io_read_lines", (int)strlen("io_read_lines"));
    push(OBJ_VAL(name));
    tableSet(&vm.globals, name, OBJ_VAL(newNative(ioReadLinesNative)));
    pop();

    name = copyString("io_write_lines", (int)strlen("io_write_lines"));
    push(OBJ_VAL(name));
    tableSet(&vm.globals, name, OBJ_VAL(newNative(ioWriteLinesNative)));
    pop();

    /* Stage 12a: array value type via natives. */
    name = copyString("array", (int)strlen("array"));
    push(OBJ_VAL(name));
    tableSet(&vm.globals, name, OBJ_VAL(newNative(arrayCreateNative)));
    pop();

    name = copyString("array_length", (int)strlen("array_length"));
    push(OBJ_VAL(name));
    tableSet(&vm.globals, name, OBJ_VAL(newNative(arrayLengthNative)));
    pop();

    name = copyString("array_get", (int)strlen("array_get"));
    push(OBJ_VAL(name));
    tableSet(&vm.globals, name, OBJ_VAL(newNative(arrayGetNative)));
    pop();

    name = copyString("array_set", (int)strlen("array_set"));
    push(OBJ_VAL(name));
    tableSet(&vm.globals, name, OBJ_VAL(newNative(arraySetNative)));
    pop();

    name = copyString("array_push", (int)strlen("array_push"));
    push(OBJ_VAL(name));
    tableSet(&vm.globals, name, OBJ_VAL(newNative(arrayPushNative)));
    pop();

    /* Stage 19: array_reverse. */
    name = copyString("array_reverse", (int)strlen("array_reverse"));
    push(OBJ_VAL(name));
    tableSet(&vm.globals, name, OBJ_VAL(newNative(arrayReverseNative)));
    pop();

    /* Stage 28: array_unique. */
    name = copyString("array_unique", (int)strlen("array_unique"));
    push(OBJ_VAL(name));
    tableSet(&vm.globals, name, OBJ_VAL(newNative(arrayUniqueNative)));
    pop();

    /* Stage 40: array_unique_by. Takes an array and a
     * 1-arg keyFn; deduplicates by the keyFn's return
     * value (preserving first-occurrence-of-each-key).
     * Architecture: reuses Stage 30's callClosureFromNative. */
    name = copyString("array_unique_by", (int)strlen("array_unique_by"));
    push(OBJ_VAL(name));
    tableSet(&vm.globals, name, OBJ_VAL(newNative(arrayUniqueByNative)));
    pop();

    /* Stage 7: type predicate. */
    name = copyString("typeof", (int)strlen("typeof"));
    push(OBJ_VAL(name));
    tableSet(&vm.globals, name, OBJ_VAL(newNative(typeofNative)));
    pop();

    /* Stage 44: type predicate. is_array(value)
     * returns true iff the value is an array. Closes
     * a 27-stage-old gap (Stage 39 added typeof(<array>)
     * returning "array" but the caller had to compare
     * a string; is_array() is the ergonomic predicate).
     * ~10 lines, no new architecture, no user-code
     * dispatch. */
    name = copyString("is_array", (int)strlen("is_array"));
    push(OBJ_VAL(name));
    tableSet(&vm.globals, name, OBJ_VAL(newNative(isArrayNative)));
    pop();

    /* Stage 45: type predicate. is_string(value)
     * returns true iff the value is a string. The
     * next type predicate after Stage 44's is_array.
     * Closes a gap (the caller had to use
     * typeof(x) == "string" to determine string-ness).
     * ~10 lines, no new architecture, no user-code
     * dispatch. The push/pop count is balanced:
     * 1 push for the name, 1 pop after tableSet. */
    name = copyString("is_string", (int)strlen("is_string"));
    push(OBJ_VAL(name));
    tableSet(&vm.globals, name, OBJ_VAL(newNative(isStringNative)));
    pop();

    /* Stage 46: type predicate. is_number(value)
     * returns true iff the value is a number. The
     * 3rd type predicate after Stage 44's is_array
     * and Stage 45's is_string. Closes a gap (the
     * caller had to use typeof(x) == "number" to
     * determine number-ness). ~10 lines, no new
     * architecture, no user-code dispatch. The
     * push/pop count is balanced: 1 push for the
     * name, 1 pop after tableSet. */
    name = copyString("is_number", (int)strlen("is_number"));
    push(OBJ_VAL(name));
    tableSet(&vm.globals, name, OBJ_VAL(newNative(isNumberNative)));
    pop();

    /* Stage 47: type predicate. is_bool(value)
     * returns true iff the value is a boolean. The
     * 4th type predicate after Stage 44's is_array,
     * Stage 45's is_string, and Stage 46's
     * is_number. Closes a gap (the caller had to
     * use typeof(x) == "bool" to determine
     * bool-ness). ~10 lines, no new architecture,
     * no user-code dispatch. The push/pop count
     * is balanced: 1 push for the name, 1 pop
     * after tableSet. */
    name = copyString("is_bool", (int)strlen("is_bool"));
    push(OBJ_VAL(name));
    tableSet(&vm.globals, name, OBJ_VAL(newNative(isBoolNative)));
    pop();

    /* Stage 48: type predicate. is_nil(value)
     * returns true iff the value is nil. The 5th
     * type predicate after Stage 44's is_array,
     * Stage 45's is_string, Stage 46's is_number,
     * and Stage 47's is_bool. Closes a gap (the
     * caller had to use typeof(x) == "nil" to
     * determine nil-ness). ~10 lines, no new
     * architecture, no user-code dispatch. The
     * push/pop count is balanced: 1 push for the
     * name, 1 pop after tableSet. */
    name = copyString("is_nil", (int)strlen("is_nil"));
    push(OBJ_VAL(name));
    tableSet(&vm.globals, name, OBJ_VAL(newNative(isNilNative)));
    pop();

    /* Stage 49: type predicate. is_function(value)
     * returns true iff the value is a function
     * (Lox closure OR C-defined native). The 6th
     * and final type predicate after Stage 44's
     * is_array, Stage 45's is_string, Stage 46's
     * is_number, Stage 47's is_bool, and Stage 48's
     * is_nil. Closes a gap (the caller had to use
     * typeof(x) == "function" to determine
     * function-ness). The new wrinkle: function-
     * ness in clox has TWO sub-types — ObjClosure
     * (Lox-defined) and ObjNative (C-defined).
     * is_function returns true for both — matches
     * the JS/Python mental model. ~15 lines, no
     * new architecture, no user-code dispatch. The
     * push/pop count is balanced: 1 push for the
     * name, 1 pop after tableSet. After Stage 49,
     * the type-predicate pattern is COMPLETE (5 of
     * 5+ natives). */
    name = copyString("is_function", (int)strlen("is_function"));
    push(OBJ_VAL(name));
    tableSet(&vm.globals, name, OBJ_VAL(newNative(isFunctionNative)));
    pop();

    /* Stage 50: array_zip_longest(arr1, arr2, fill?)
     * -> array. Like Stage 37's array_zip but pads
     * the shorter with a default. ~50 lines, no new
     * architecture, no user-code dispatch. Reuses
     * Stage 37's array_zip + newArray + arrayPush +
     * GC-keep-alive pattern. The new wrinkle: this
     * is the FIRST clox native with an OPTIONAL
     * positional parameter (the fill value). clox's
     * native API uses (int argCount, Value *args) —
     * the optional pattern is "if (argCount > 2)
     * use args[2] else use NIL_VAL". After Stage
     * 49, the type-predicate pattern is COMPLETE
     * (is_array, is_string, is_number, is_bool,
     * is_nil, is_function). Stage 50 starts a new
     * family: "combinators that take optional
     * defaults" (next candidates: array_take(arr,
     * n?), array_drop(arr, n?)). The push/pop count
     * is balanced: 1 push for the name, 1 pop after
     * tableSet. After Stage 50, the next decision
     * is array_take / array_drop (slice operations)
     * or array_take_while / array_drop_while
     * (short-circuit slice operations) or
     * array_intersect / array_union / array_difference
     * (set operations) or modules (~600 lines,
     * Tom's call) or grow the trigger-mine bucket
     * beyond byox. */
    name = copyString("array_zip_longest", (int)strlen("array_zip_longest"));
    push(OBJ_VAL(name));
    tableSet(&vm.globals, name, OBJ_VAL(newNative(arrayZipLongestNative)));
    pop();

    /* Stage 51: array_take. Takes an array and a
     * count, returns the first N elements. ~30
     * lines, no new architecture, no user-code
     * dispatch. The new wrinkle: a "size limit"
     * pattern, similar to Stage 41's array_chunk.
     * The push/pop count is balanced: 1 push for
     * the name, 1 pop after tableSet. */
    name = copyString("array_take", (int)strlen("array_take"));
    push(OBJ_VAL(name));
    tableSet(&vm.globals, name, OBJ_VAL(newNative(arrayTakeNative)));
    pop();

    /* Stage 52: array_drop. The complement of
     * Stage 51's array_take: drop the first N
     * elements, return the rest. ~30 lines, no new
     * architecture, no user-code dispatch. The
     * new wrinkle: the "skip the first N" pattern.
     * The push/pop count is balanced: 1 push for
     * the name, 1 pop after tableSet. */
    name = copyString("array_drop", (int)strlen("array_drop"));
    push(OBJ_VAL(name));
    tableSet(&vm.globals, name, OBJ_VAL(newNative(arrayDropNative)));
    pop();
}
