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
     *   string_split("a", "")  -> ["a"]     (empty delim = whole string) */
    if (argCount != 2) {
        runtimeError("string_split() takes 2 arguments (%d given).", argCount);
        return NIL_VAL;
    }
    if (!IS_STRING(args[0]) || !IS_STRING(args[1])) {
        runtimeError("string_split() arguments must be strings.");
        return NIL_VAL;
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
    while (i <= s->length - delim->length) {
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
        } else {
            i++;
        }
    }
    /* Push the tail s[prev..s->length]. This is empty if the string
     * ended at a delim boundary (trailing empty). */
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
    arrayPush(AS_ARRAY(args[0]), args[1]);
    return NIL_VAL;
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

    /* Stage 7: type predicate. */
    name = copyString("typeof", (int)strlen("typeof"));
    push(OBJ_VAL(name));
    tableSet(&vm.globals, name, OBJ_VAL(newNative(typeofNative)));
    pop();
}
