/*
 * clox — Stdlib integration tests
 *
 * Drives `./bin/clox` as a subprocess via popen with small Lox scripts
 * that exercise the built-in native functions, and asserts on the
 * combined stdout+stderr output.
 *
 * Style mirrors tests/test_clox.c and tests/test_repl.c.
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>
#include <math.h>
#include <sys/wait.h>

static int g_testCounter = 0;
static int g_passed = 0;
static int g_failed = 0;

static void fail(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
    g_failed++;
}

static void pass(void) {
    g_passed++;
}

static char* runClox(const char* source, int* exitCode) {
    char path[256];
    snprintf(path, sizeof(path), "/tmp/clox_stdlib_test_%d.lox", g_testCounter++);

    FILE* f = fopen(path, "w");
    if (f == NULL) {
        fprintf(stderr, "Failed to create temp file.\n");
        exit(1);
    }
    fputs(source, f);
    fclose(f);

    char cmd[512];
    snprintf(cmd, sizeof(cmd), "./bin/clox %s 2>&1", path);

    FILE* pipe = popen(cmd, "r");
    if (pipe == NULL) {
        fprintf(stderr, "Failed to run clox.\n");
        exit(1);
    }

    size_t capacity = 256;
    size_t length = 0;
    char* output = (char*)malloc(capacity);
    if (output == NULL) {
        fprintf(stderr, "Out of memory.\n");
        exit(1);
    }

    for (;;) {
        size_t remaining = capacity - length - 1;
        if (remaining < 64) {
            capacity *= 2;
            output = (char*)realloc(output, capacity);
            if (output == NULL) {
                fprintf(stderr, "Out of memory.\n");
                exit(1);
            }
            remaining = capacity - length - 1;
        }
        size_t n = fread(output + length, 1, remaining, pipe);
        length += n;
        if (n == 0) break;
    }
    output[length] = '\0';

    int status = pclose(pipe);
    *exitCode = WEXITSTATUS(status);
    remove(path);
    return output;
}

static bool contains(const char* haystack, const char* needle) {
    return strstr(haystack, needle) != NULL;
}

/* --- The tests --- */

static void test_clock_exists(void) {
    /* clock() already shipped in stage 05. Smoke test. */
    int exitCode;
    char* out = runClox("print clock();\n", &exitCode);
    if (exitCode != 0) {
        fail("stdlib/clock: expected exit 0, got %d (output: %s)", exitCode, out);
    } else {
        /* Should print some number > 0 (a CPU-time-ish value). */
        bool sawDigits = false;
        for (const char* p = out; *p; p++) {
            if (*p >= '0' && *p <= '9') { sawDigits = true; break; }
        }
        if (!sawDigits) {
            fail("stdlib/clock: expected a number in output, got '%s'", out);
        } else {
            pass();
        }
    }
    free(out);
}

static void test_number_abs(void) {
    int exitCode;
    char* out = runClox(
        "print number_abs(-7);\n"
        "print number_abs(0);\n"
        "print number_abs(3.5);\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/abs: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "7") || !contains(out, "0") || !contains(out, "3.5")) {
        fail("stdlib/abs: expected 7, 0, 3.5 in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_number_min_max(void) {
    int exitCode;
    char* out = runClox(
        "print number_min(3, 7);\n"
        "print number_max(3, 7);\n"
        "print number_min(-1.5, -2.5);\n"
        "print number_max(-1.5, -2.5);\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/min-max: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "3") || !contains(out, "7") ||
               !contains(out, "-2.5") || !contains(out, "-1.5")) {
        fail("stdlib/min-max: expected 3, 7, -2.5, -1.5 in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_string_length(void) {
    int exitCode;
    char* out = runClox(
        "print string_length(\"\");\n"
        "print string_length(\"hi\");\n"
        "print string_length(\"hello\");\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/length: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "0") || !contains(out, "2") || !contains(out, "5")) {
        fail("stdlib/length: expected 0, 2, 5 in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_string_upper_lower(void) {
    int exitCode;
    char* out = runClox(
        "print string_upper(\"hello\");\n"
        "print string_lower(\"WORLD\");\n"
        "print string_upper(\"MiXeD\");\n"
        "print string_lower(\"MiXeD\");\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/upper-lower: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "HELLO") || !contains(out, "world") ||
               !contains(out, "MIXED") || !contains(out, "mixed")) {
        fail("stdlib/upper-lower: expected HELLO, world, MIXED, mixed in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_type_predicate(void) {
    int exitCode;
    char* out = runClox(
        "print typeof(1);\n"
        "print typeof(1.5);\n"
        "print typeof(\"hi\");\n"
        "print typeof(true);\n"
        "print typeof(nil);\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/typeof: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "number") || !contains(out, "string") ||
               !contains(out, "bool") || !contains(out, "nil")) {
        fail("stdlib/typeof: expected 'number', 'string', 'bool', 'nil' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_typeof_array(void) {
    /* Stage 39: typeof(<array>) returns "array" (not
     * "object"). Before this patch, typeofNative's switch
     * didn't enumerate OBJ_ARRAY, so the default "object"
     * was returned. The fix adds 1 case to the switch.
     *
     * Test cases:
     * - empty array: typeof([]) == "array"
     * - populated array: typeof([1, 2, 3]) == "array"
     * - nested array: typeof([[1, 2], [3, 4]]) == "array"
     *   (the outer array is still an array; the inner
     *   arrays don't change the outer's type) */
    int exitCode;
    char *out = runClox(
        "print typeof([]);\n"
        "print typeof([1, 2, 3]);\n"
        "print typeof([[1, 2], [3, 4]]);\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/typeof-array: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "array\narray\narray\n")) {
        fail("stdlib/typeof-array: expected 'array' 3 times, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_type_wrong_arg_count(void) {
    /* Defensive: native function should handle wrong arg count. */
    int exitCode;
    char* out = runClox("print number_abs();\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/abs-no-args: expected nonzero exit on arity error, got 0 (output: %s)", out);
    } else {
        pass();
    }
    free(out);
}

/* --- Stage 8 tests: more string operations --- */

static void test_string_substring(void) {
    /* string_substring(s, start, end) — end is exclusive. */
    int exitCode;
    char* out = runClox(
        "print string_substring(\"hello\", 0, 5);\n"   /* "hello" */
        "print string_substring(\"hello\", 0, 0);\n"   /* ""      */
        "print string_substring(\"hello\", 1, 4);\n"   /* "ell"   */
        "print string_substring(\"hello\", 2, 2);\n",  /* ""      */
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/substring: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "hello") || !contains(out, "ell")) {
        fail("stdlib/substring: expected 'hello' and 'ell' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_string_substring_clamp(void) {
    /* Out-of-range start/end should be clamped, not crash. */
    int exitCode;
    char* out = runClox(
        "print string_substring(\"hi\", 0, 100);\n"   /* full string  */
        "print string_substring(\"hi\", -5, 2);\n"    /* full string  */
        "print string_substring(\"hi\", 100, 200);\n",/* empty string */
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/substring-clamp: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "hi")) {
        fail("stdlib/substring-clamp: expected 'hi' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_string_contains(void) {
    /* string_contains(haystack, needle) — boolean. */
    int exitCode;
    char* out = runClox(
        "print string_contains(\"hello world\", \"world\");\n"
        "print string_contains(\"hello world\", \"xyz\");\n"
        "print string_contains(\"hello\", \"\");\n",     /* empty is always contained */
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/contains: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "true") || !contains(out, "false")) {
        fail("stdlib/contains: expected 'true' and 'false' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_string_replace(void) {
    /* string_replace(s, old, new) — replace first occurrence. */
    int exitCode;
    char* out = runClox(
        "print string_replace(\"hello world\", \"world\", \"there\");\n"
        "print string_replace(\"aaaa\", \"aa\", \"b\");\n"   /* "baa"  */
        "print string_replace(\"hello\", \"x\", \"y\");\n",   /* unchanged */
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/replace: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "hello there") || !contains(out, "baa")) {
        fail("stdlib/replace: expected 'hello there' and 'baa' in output, got '%s'", out);
    } else if (!contains(out, "hello\n")) {
        fail("stdlib/replace: expected the unchanged 'hello' line in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_string_replace_wrong_args(void) {
    /* Arity error path on a new native. */
    int exitCode;
    char* out = runClox("print string_contains(\"hi\");\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/contains-wrong-args: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);
}

/* --- Stage 9 tests: starts_with, ends_with, index_of, trim --- */

static void test_string_starts_with(void) {
    int exitCode;
    char* out = runClox(
        "print string_starts_with(\"hello world\", \"hello\");\n"
        "print string_starts_with(\"hello world\", \"world\");\n"
        "print string_starts_with(\"hello\", \"\");\n",      /* empty always matches */
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/starts-with: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "true") || !contains(out, "false")) {
        fail("stdlib/starts-with: expected 'true' and 'false' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_string_ends_with(void) {
    int exitCode;
    char* out = runClox(
        "print string_ends_with(\"hello world\", \"world\");\n"
        "print string_ends_with(\"hello world\", \"hello\");\n"
        "print string_ends_with(\"hello\", \"\");\n",       /* empty always matches */
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/ends-with: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "true") || !contains(out, "false")) {
        fail("stdlib/ends-with: expected 'true' and 'false' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_string_index_of(void) {
    /* string_index_of(haystack, needle) — returns -1 if not found, else position. */
    int exitCode;
    char* out = runClox(
        "print string_index_of(\"hello world\", \"world\");\n"   /* 6  */
        "print string_index_of(\"hello world\", \"hello\");\n"   /* 0  */
        "print string_index_of(\"hello world\", \"xyz\");\n"     /* -1 */
        "print string_index_of(\"hello\", \"\");\n",             /* 0  (empty matches at 0) */
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/index-of: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "6") || !contains(out, "0\n") ||
               !contains(out, "-1")) {
        fail("stdlib/index-of: expected 6, 0, and -1 in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_string_trim(void) {
    /* string_trim(s) — strip leading and trailing whitespace.
     * Note: clox strings don't have escape sequences, so we test with
     * literal spaces (and the all-whitespace edge case). */
    int exitCode;
    char* out = runClox(
        "print string_trim(\"  hello  \");\n"
        "print string_trim(\"\");\n"
        "print string_trim(\"   \");\n"             /* all whitespace -> empty */
        "print string_trim(\"no whitespace\");\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/trim: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "hello\n") || !contains(out, "no whitespace")) {
        fail("stdlib/trim: expected trimmed 'hello' and 'no whitespace' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

/* --- Stage 10 tests: more number operations --- */

static void test_number_floor_ceil_round(void) {
    int exitCode;
    char* out = runClox(
        "print number_floor(3.7);\n"   /* 3  */
        "print number_floor(-2.3);\n"  /* -3 */
        "print number_floor(5.0);\n"   /* 5  */
        "print number_ceil(3.2);\n"    /* 4  */
        "print number_ceil(-2.7);\n"   /* -2 */
        "print number_round(3.5);\n"   /* 4  (banker's rounding? no — C's round() rounds half away from zero, so 3.5 -> 4) */
        "print number_round(3.4);\n",  /* 3  */
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/floor-ceil-round: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "3\n") || !contains(out, "-3") ||
               !contains(out, "4\n") || !contains(out, "-2")) {
        fail("stdlib/floor-ceil-round: expected 3, -3, 4, -2 in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_number_sqrt(void) {
    int exitCode;
    char* out = runClox(
        "print number_sqrt(16);\n"    /* 4   */
        "print number_sqrt(2);\n"     /* 1.4142... */
        "print number_sqrt(0);\n",    /* 0   */
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/sqrt: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "4") || !contains(out, "0\n")) {
        fail("stdlib/sqrt: expected 4 and 0 in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_number_sqrt_negative(void) {
    /* Sqrt of a negative is a runtime error, not a successful call.
     * Run as a separate test that expects nonzero exit. */
    int exitCode;
    char* out = runClox("print number_sqrt(-1);\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/sqrt-negative: expected nonzero exit on negative input, got 0 (output: %s)", out);
    } else if (!contains(out, "non-negative")) {
        fail("stdlib/sqrt-negative: expected 'non-negative' in error output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_number_pow(void) {
    int exitCode;
    char* out = runClox(
        "print number_pow(2, 10);\n"  /* 1024 */
        "print number_pow(3, 0);\n"   /* 1    */
        "print number_pow(5, -1);\n"  /* 0.2  */
        "print number_pow(0, 0);\n",  /* 1 (by convention) */
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/pow: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "1024") || !contains(out, "0.2")) {
        fail("stdlib/pow: expected 1024 and 0.2 in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_number_wrong_args(void) {
    /* Arity error path on a new native. */
    int exitCode;
    char* out = runClox("print number_sqrt();\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/sqrt-wrong-args: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);
}

/* --- Stage 11 tests: I/O natives --- */

static void test_io_print(void) {
    /* io_print(s) writes to stdout (same channel as print).
     * Without redirection we can only assert exit code 0. */
    int exitCode;
    char* out = runClox(
        "io_print(\"hello, stdout\\n\");\n"
        "io_print(\"\");\n"
        "io_print(\"trailing\");\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/io-print: expected exit 0, got %d (output: %s)", exitCode, out);
    } else {
        pass();
    }
    free(out);
}

static void test_io_eprint(void) {
    /* io_eprint(s) writes to stderr. In script mode stdout and stderr
     * both end up in our popen pipe (stderr is line-buffered, so order
     * is preserved in practice). We assert both channels reach us. */
    int exitCode;
    char* out = runClox(
        "io_eprint(\"to stderr\\n\");\n"
        "io_eprint(\"still stderr\");\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/io-eprint: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "to stderr") || !contains(out, "still stderr")) {
        fail("stdlib/io-eprint: expected both stderr lines in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_io_wrong_args(void) {
    /* Arity error on a new native. */
    int exitCode;
    char* out = runClox("io_print();\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/io-print-wrong-args: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);
}

static void test_io_wrong_type(void) {
    /* Type error on a new native. */
    int exitCode;
    char* out = runClox("io_print(42);\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/io-print-wrong-type: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);
}

static void test_io_exit(void) {
    /* io_exit(code) terminates the script with the given exit code.
     * Negative: positive; zero: success; positive: error code.
     * Run as a separate test that expects the explicit exit code. */
    int exitCode;
    char* out = runClox(
        "io_exit(42);\n"
        "print \"unreachable\";\n",  /* must NOT run */
        &exitCode);
    if (exitCode != 42) {
        fail("stdlib/io-exit: expected exit 42, got %d (output: %s)", exitCode, out);
    } else if (contains(out, "unreachable")) {
        fail("stdlib/io-exit: 'unreachable' should not have printed, output: %s", out);
    } else {
        pass();
    }
    free(out);
}

static void test_io_read_line(void) {
    /* io_read_line() reads a line from stdin. The test framework's
     * popen("w") opens a write pipe to the child, so we can't read the
     * child's stdin from us. Instead, we write a one-shot script to /tmp
     * and invoke clox with shell input redirection. */
    const char *scriptPath = "/tmp/clox_s11_readline_test.lox";
    FILE *f = fopen(scriptPath, "w");
    if (f == NULL) {
        fail("stdlib/io-read-line: could not write test script");
        return;
    }
    fputs(
        "var line = io_read_line();\n"
        "if (line == \"hello stdin\") {\n"
        "  print \"matched\";\n"
        "} else {\n"
        "  print \"mismatch\";\n"
        "}\n",
        f);
    fclose(f);

    char cmd[512];
    snprintf(cmd, sizeof(cmd), "echo 'hello stdin' | %s/bin/clox %s",
             "/home/mira/build-your-own-x/05-vm/clox", scriptPath);

    FILE *pipe = popen(cmd, "r");
    if (pipe == NULL) {
        fail("stdlib/io-read-line: popen failed");
        return;
    }

    char out[1024] = {0};
    size_t n = fread(out, 1, sizeof(out) - 1, pipe);
    out[n] = '\0';
    int exitCode = pclose(pipe);

    if (exitCode != 0) {
        fail("stdlib/io-read-line: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "matched")) {
        fail("stdlib/io-read-line: expected 'matched' in output, got '%s'", out);
    } else {
        pass();
    }
}

/* --- Stage 12a tests: array value type via natives --- */

static void test_array_create_and_length(void) {
    /* array(arg1, arg2, ...) -> ObjArray
     * array_length(arr) -> number */
    int exitCode;
    char* out = runClox(
        "var a = array(1, 2, 3);\n"
        "print array_length(a);\n"
        "var b = array();\n"     /* empty array */
        "print array_length(b);\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-create-length: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "3\n") || !contains(out, "0\n")) {
        fail("stdlib/array-create-length: expected '3' and '0' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_get_set(void) {
    /* array_get(arr, i) -> element; runtime error if out of bounds
     * array_set(arr, i, val) -> modifies arr; runtime error if OOB */
    int exitCode;
    char* out = runClox(
        "var a = array(10, 20, 30);\n"
        "print array_get(a, 0);\n"      /* 10 */
        "print array_get(a, 1);\n"      /* 20 */
        "print array_get(a, 2);\n"      /* 30 */
        "array_set(a, 1, 99);\n"
        "print array_get(a, 1);\n",     /* 99 */
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-get-set: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "10\n") || !contains(out, "20\n") ||
               !contains(out, "30\n") || !contains(out, "99\n")) {
        fail("stdlib/array-get-set: expected 10,20,30,99 in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_push(void) {
    /* array_push(arr, val) -> modifies arr; arr now has one more element */
    int exitCode;
    char* out = runClox(
        "var a = array(1, 2);\n"
        "print array_length(a);\n"     /* 2 */
        "array_push(a, 3);\n"
        "print array_length(a);\n"     /* 3 */
        "print array_get(a, 2);\n",    /* 3 */
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-push: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "2\n") || !contains(out, "3\n")) {
        fail("stdlib/array-push: expected '2' and '3' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_get_out_of_bounds(void) {
    /* Runtime error: array_get(a, 5) on a 3-element array. */
    int exitCode;
    char* out = runClox(
        "var a = array(1, 2, 3);\n"
        "print array_get(a, 5);\n",
        &exitCode);
    if (exitCode == 0) {
        fail("stdlib/array-oob-get: expected nonzero exit, got 0");
    } else if (!contains(out, "out of bounds") && !contains(out, "Index")) {
        fail("stdlib/array-oob-get: expected 'out of bounds' or 'Index' in error, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_set_out_of_bounds(void) {
    /* Runtime error: array_set(a, 5, 99) on a 3-element array. */
    int exitCode;
    char* out = runClox(
        "var a = array(1, 2, 3);\n"
        "array_set(a, 5, 99);\n",
        &exitCode);
    if (exitCode == 0) {
        fail("stdlib/array-oob-set: expected nonzero exit, got 0");
    } else if (!contains(out, "out of bounds") && !contains(out, "Index")) {
        fail("stdlib/array-oob-set: expected 'out of bounds' or 'Index' in error, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_wrong_args(void) {
    /* Type error: array_length() called with a number. */
    int exitCode;
    char* out = runClox("print array_length(42);\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/array-wrong-args: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);
}

/* --- Stage 12b-i tests: array literals [1, 2, 3] --- */

static void test_array_literal_3(void) {
    /* [1, 2, 3] builds a 3-element array. */
    int exitCode;
    char* out = runClox(
        "var a = [1, 2, 3];\n"
        "print array_length(a);\n"     /* 3 */
        "print array_get(a, 0);\n"     /* 1 */
        "print array_get(a, 1);\n"     /* 2 */
        "print array_get(a, 2);\n",    /* 3 */
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-literal-3: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "3\n") || !contains(out, "1\n") ||
               !contains(out, "2\n")) {
        fail("stdlib/array-literal-3: expected 3, 1, 2, 3 in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_literal_empty(void) {
    /* [] builds a 0-element array. */
    int exitCode;
    char* out = runClox(
        "var a = [];\n"
        "print array_length(a);\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-literal-empty: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "0\n")) {
        fail("stdlib/array-literal-empty: expected '0' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_literal_mixed(void) {
    /* Mixed-type literal: numbers and strings in one array. */
    int exitCode;
    char* out = runClox(
        "var a = [1, \"foo\", true];\n"
        "print array_length(a);\n"     /* 3 */
        "print array_get(a, 0);\n"     /* 1 */
        "print array_get(a, 1);\n"     /* foo (no quotes) */
        "print array_get(a, 2);\n",    /* true */
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-literal-mixed: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "3\n") || !contains(out, "1\n") ||
               !contains(out, "foo\n") || !contains(out, "true\n")) {
        fail("stdlib/array-literal-mixed: expected 3, 1, foo, true in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_literal_nested(void) {
    /* Nested literal: [[1, 2], [3, 4]] is a 2-element array of arrays. */
    int exitCode;
    char* out = runClox(
        "var a = [[1, 2], [3, 4]];\n"
        "print array_length(a);\n"             /* 2 */
        "print array_length(array_get(a, 0));\n" /* 2 */
        "print array_length(array_get(a, 1));\n" /* 2 */
        "print array_get(array_get(a, 0), 1);\n",/* 2 */
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-literal-nested: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "2\n")) {
        fail("stdlib/array-literal-nested: expected multiple '2's in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_print_array_literal(void) {
    /* print [1, 2, 3] should output [1, 2, 3]. */
    int exitCode;
    char* out = runClox("print [1, 2, 3];\n", &exitCode);
    if (exitCode != 0) {
        fail("stdlib/print-array-literal: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "[1, 2, 3]")) {
        fail("stdlib/print-array-literal: expected '[1, 2, 3]' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

/* --- Stage 12b-ii tests: a[i] index read --- */

static void test_array_index_read(void) {
    /* a[i] reads the i-th element. */
    int exitCode;
    char* out = runClox(
        "var a = [10, 20, 30];\n"
        "print a[0];\n"   /* 10 */
        "print a[1];\n"   /* 20 */
        "print a[2];\n",  /* 30 */
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-index-read: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "10\n") || !contains(out, "20\n") ||
               !contains(out, "30\n")) {
        fail("stdlib/array-index-read: expected 10, 20, 30 in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_index_nested(void) {
    /* a[i][j] reads a 2D array. */
    int exitCode;
    char* out = runClox(
        "var grid = [[1, 2, 3], [4, 5, 6]];\n"
        "print grid[0][0];\n"  /* 1 */
        "print grid[0][2];\n"  /* 3 */
        "print grid[1][1];\n"  /* 5 */
        "print grid[1][2];\n", /* 6 */
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-index-nested: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "1\n") || !contains(out, "3\n") ||
               !contains(out, "5\n") || !contains(out, "6\n")) {
        fail("stdlib/array-index-nested: expected 1, 3, 5, 6 in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_index_expression(void) {
    /* Index can be an expression: a[i+1], a[i*2]. */
    int exitCode;
    char* out = runClox(
        "var a = [10, 20, 30, 40, 50];\n"
        "var i = 2;\n"
        "print a[i];\n"       /* 30 */
        "print a[i + 1];\n"   /* 40 */
        "print a[i * 2];\n",  /* 50 */
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-index-expression: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "30\n") || !contains(out, "40\n") ||
               !contains(out, "50\n")) {
        fail("stdlib/array-index-expression: expected 30, 40, 50 in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_index_out_of_bounds(void) {
    /* a[5] on a 3-element array is a runtime error. */
    int exitCode;
    char* out = runClox(
        "var a = [1, 2, 3];\n"
        "print a[5];\n",
        &exitCode);
    if (exitCode == 0) {
        fail("stdlib/array-index-oob: expected nonzero exit, got 0");
    } else if (!contains(out, "out of bounds") && !contains(out, "Index")) {
        fail("stdlib/array-index-oob: expected 'out of bounds' in error, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_index_negative(void) {
    /* a[-1] is a runtime error (we don't support negative indexing). */
    int exitCode;
    char* out = runClox(
        "var a = [1, 2, 3];\n"
        "print a[-1];\n",
        &exitCode);
    if (exitCode == 0) {
        fail("stdlib/array-index-negative: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);
}

static void test_array_index_wrong_type(void) {
    /* a["foo"] is a runtime error: index must be a number. */
    int exitCode;
    char* out = runClox(
        "var a = [1, 2, 3];\n"
        "print a[\"foo\"];\n",
        &exitCode);
    if (exitCode == 0) {
        fail("stdlib/array-index-wrong-type: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);
}

static void test_array_index_on_non_array(void) {
    /* 42[0] is a runtime error: subscript requires an array. */
    int exitCode;
    char* out = runClox("print 42[0];\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/array-index-on-non-array: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);
}

/* --- Stage 12b-iii tests: a[i] = v index write --- */

static void test_array_index_write(void) {
    /* a[i] = v assigns the v-th element. */
    int exitCode;
    char* out = runClox(
        "var a = [1, 2, 3];\n"
        "a[0] = 99;\n"
        "a[2] = 77;\n"
        "print a[0];\n"  /* 99 */
        "print a[1];\n"  /* 2 */
        "print a[2];\n", /* 77 */
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-index-write: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "99\n") || !contains(out, "2\n") || !contains(out, "77\n")) {
        fail("stdlib/array-index-write: expected 99, 2, 77 in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_index_write_expression(void) {
    /* Right side of = is a full expression. */
    int exitCode;
    char* out = runClox(
        "var a = [1, 2, 3, 4, 5];\n"
        "var i = 1;\n"
        "a[i] = 100;\n"
        "a[i + 1] = 200;\n"
        "a[2 * 2] = 300;\n"
        "print a[0];\n"  /* 1 */
        "print a[1];\n"  /* 100 */
        "print a[2];\n"  /* 200 */
        "print a[3];\n"  /* 4 */
        "print a[4];\n", /* 300 */
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-index-write-expr: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "1\n") || !contains(out, "100\n") ||
               !contains(out, "200\n") || !contains(out, "4\n") ||
               !contains(out, "300\n")) {
        fail("stdlib/array-index-write-expr: expected 1, 100, 200, 4, 300 in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_index_write_then_read(void) {
    /* Chained write then read: a[0] = a[1]. */
    int exitCode;
    char* out = runClox(
        "var a = [1, 2, 3];\n"
        "a[0] = a[1];\n"
        "print a[0];\n"  /* 2 */
        "print a[1];\n"  /* 2 */
        "print a[2];\n", /* 3 */
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-index-write-then-read: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "2\n") || !contains(out, "3\n")) {
        fail("stdlib/array-index-write-then-read: expected 2, 2, 3 in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_index_write_oob(void) {
    /* a[5] = v on a 3-element array is a runtime error. */
    int exitCode;
    char* out = runClox(
        "var a = [1, 2, 3];\n"
        "a[5] = 99;\n",
        &exitCode);
    if (exitCode == 0) {
        fail("stdlib/array-index-write-oob: expected nonzero exit, got 0");
    } else if (!contains(out, "out of bounds")) {
        fail("stdlib/array-index-write-oob: expected 'out of bounds' in error, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_index_write_on_non_array(void) {
    /* 42[0] = v is a runtime error: subscript requires an array. */
    int exitCode;
    char* out = runClox("42[0] = 99;\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/array-index-write-on-non-array: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);
}

/* --- Stage 13 tests: string_split / string_join --- */

static void test_string_split_basic(void) {
    /* string_split("a,b,c", ",") -> ["a", "b", "c"] */
    int exitCode;
    char* out = runClox(
        "var parts = string_split(\"a,b,c\", \",\");\n"
        "print(parts[0]); print(parts[1]); print(parts[2]);\n"
        "print(array_length(parts));\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/string-split-basic: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "a\n") || !contains(out, "b\n") || !contains(out, "c\n") ||
               !contains(out, "3\n")) {
        fail("stdlib/string-split-basic: expected a, b, c, 3 in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_string_split_empty_delim(void) {
    /* string_split("hello", "") -> ["hello"] (single element). */
    int exitCode;
    char* out = runClox(
        "var parts = string_split(\"hello\", \"\");\n"
        "print(array_length(parts));\n"
        "print(parts[0]);\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/string-split-empty-delim: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "1\n") || !contains(out, "hello\n")) {
        fail("stdlib/string-split-empty-delim: expected 1, hello in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_string_split_empty_string(void) {
    /* string_split("", ",") -> [] (empty array). */
    int exitCode;
    char* out = runClox(
        "var parts = string_split(\"\", \",\");\n"
        "print(array_length(parts));\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/string-split-empty-string: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "0\n")) {
        fail("stdlib/string-split-empty-string: expected 0 in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_string_split_no_match(void) {
    /* string_split("hello", ",") -> ["hello"] (no occurrences of delim). */
    int exitCode;
    char* out = runClox(
        "var parts = string_split(\"hello\", \",\");\n"
        "print(array_length(parts));\n"
        "print(parts[0]);\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/string-split-no-match: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "1\n") || !contains(out, "hello\n")) {
        fail("stdlib/string-split-no-match: expected 1, hello in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_string_split_multi_char(void) {
    /* string_split("a::b::c", "::") -> ["a", "b", "c"] (multi-char delim). */
    int exitCode;
    char* out = runClox(
        "var parts = string_split(\"a::b::c\", \"::\");\n"
        "print(array_length(parts));\n"
        "print(parts[0]); print(parts[1]); print(parts[2]);\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/string-split-multi-char: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "3\n") || !contains(out, "a\n") ||
               !contains(out, "b\n") || !contains(out, "c\n")) {
        fail("stdlib/string-split-multi-char: expected 3, a, b, c in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_string_join_basic(void) {
    /* string_join(["a", "b", "c"], ",") -> "a,b,c" */
    int exitCode;
    char* out = runClox(
        "var s = string_join([\"a\", \"b\", \"c\"], \",\");\n"
        "print(s);\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/string-join-basic: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "a,b,c\n")) {
        fail("stdlib/string-join-basic: expected 'a,b,c' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_string_join_empty_array(void) {
    /* string_join([], ",") -> "" */
    int exitCode;
    char* out = runClox(
        "var s = string_join([], \",\");\n"
        "print(string_length(s));\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/string-join-empty-array: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "0\n")) {
        fail("stdlib/string-join-empty-array: expected 0 in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_string_join_single_element(void) {
    /* string_join(["only"], ",") -> "only" (no delim used). */
    int exitCode;
    char* out = runClox(
        "var s = string_join([\"only\"], \",\");\n"
        "print(s);\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/string-join-single: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "only\n")) {
        fail("stdlib/string-join-single: expected 'only' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_string_split_join_round_trip(void) {
    /* string_join(string_split(s, ","), ",") == s. */
    int exitCode;
    char* out = runClox(
        "var original = \"one,two,three,four\";\n"
        "var parts = string_split(original, \",\");\n"
        "var rejoined = string_join(parts, \",\");\n"
        "print(rejoined);\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/string-split-join-roundtrip: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "one,two,three,four\n")) {
        fail("stdlib/string-split-join-roundtrip: expected 'one,two,three,four' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_string_split_gc_stress(void) {
    /* GC stress: 200 iterations of split+join+push. Each iteration
     * creates an array of substrings and a joined string. The pieces
     * are pushed into a growing array (which forces collection) and
     * the iteration variable is reassigned. All allocs must balance
     * with frees; valgrind verifies. */
    int exitCode;
    char* out = runClox(
        "var s = \"alpha,beta,gamma,delta,epsilon,zeta,eta,theta\";\n"
        "var sink = [];\n"
        "var i = 0;\n"
        "while (i < 200) {\n"
        "    var parts = string_split(s, \",\");\n"
        "    var joined = string_join(parts, \",\");\n"
        "    array_push(sink, joined);\n"
        "    i = i + 1;\n"
        "}\n"
        "print(array_length(sink));\n"
        "print(sink[0]);\n"
        "print(sink[199]);\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/string-split-gc-stress: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "200\n") ||
               !contains(out, "alpha,beta,gamma,delta,epsilon,zeta,eta,theta\n")) {
        fail("stdlib/string-split-gc-stress: expected 200 and the round-trip string in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_string_split_wrong_type(void) {
    /* string_split(42, ",") is a runtime error. */
    int exitCode;
    char* out = runClox("string_split(42, \",\");\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/string-split-wrong-type: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);
}

static void test_string_join_wrong_type(void) {
    /* string_join(42, ",") is a runtime error: first arg must be array. */
    int exitCode;
    char* out = runClox("string_join(42, \",\");\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/string-join-wrong-type: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);
}

static void test_string_join_non_string_element(void) {
    /* string_join(["a", 42, "c"], ",") is a runtime error. */
    int exitCode;
    char* out = runClox("string_join([\"a\", 42, \"c\"], \",\");\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/string-join-non-string-element: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);
}

/* --- Stage 14 tests: io_read_file / io_write_file / io_file_exists ---
 *
 * The file I/O tests use fixed paths under /tmp and clean up after
 * themselves. Each test creates a unique file, runs the operation,
 * and removes the file. This avoids interference between tests if
 * they ever run in parallel and avoids leaving junk in /tmp.
 *
 * The `runCloxWithStdin`-style helper isn't needed because file I/O
 * tests don't pipe stdin — they use the file system directly. The
 * existing `runClox` helper (which captures stdout+stderr via popen)
 * is sufficient.
 *
 * The /tmp paths use PID to keep tests independent. */

#include <unistd.h>   /* unlink, getpid */
#include <sys/stat.h> /* stat */

/* Helper: write `content` to `path`. Returns 0 on success, -1 on failure. */
static int writeFileToDisk(const char *path, const char *content) {
    FILE *f = fopen(path, "wb");
    if (f == NULL) return -1;
    size_t len = strlen(content);
    size_t written = fwrite(content, 1, len, f);
    fclose(f);
    return (written == len) ? 0 : -1;
}

/* Helper: build a unique /tmp path for a test. */
static void buildTmpPath(char *buf, size_t bufsz, const char *suffix) {
    snprintf(buf, bufsz, "/tmp/clox_s14_%d_%s", (int)getpid(), suffix);
}

/* --- io_read_file tests --- */

static void test_io_read_file_basic(void) {
    /* Read a known file. Write it first, then read it via clox. */
    char path[256];
    buildTmpPath(path, sizeof(path), "read_basic.txt");
    const char *expected = "hello, file system\nline 2\n";
    if (writeFileToDisk(path, expected) != 0) {
        fail("stdlib/io-read-file-basic: could not seed test file");
        return;
    }
    char script[1024];
    snprintf(script, sizeof(script),
        "var contents = io_read_file(\"%s\");\n"
        "if (contents == nil) {\n"
        "  print \"read returned nil\";\n"
        "} else {\n"
        "  print contents;\n"
        "}\n",
        path);
    int exitCode;
    char *out = runClox(script, &exitCode);

    if (exitCode != 0) {
        fail("stdlib/io-read-file-basic: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "hello, file system") || !contains(out, "line 2")) {
        fail("stdlib/io-read-file-basic: expected file contents in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
    unlink(path);
}

static void test_io_read_file_nonexistent(void) {
    /* Read a file that doesn't exist -> returns nil. */
    int exitCode;
    char *out = runClox(
        "var contents = io_read_file(\"/tmp/clox_s14_does_not_exist_xyz_12345.txt\");\n"
        "if (contents == nil) {\n"
        "  print \"ok\";\n"
        "} else {\n"
        "  print \"unexpectedly read a file\";\n"
        "}\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/io-read-file-nonexistent: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "ok\n")) {
        fail("stdlib/io-read-file-nonexistent: expected 'ok' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_io_read_file_empty(void) {
    /* Read an existing but empty file -> returns "" (empty string), not nil. */
    char path[256];
    buildTmpPath(path, sizeof(path), "read_empty.txt");
    if (writeFileToDisk(path, "") != 0) {
        fail("stdlib/io-read-file-empty: could not seed test file");
        return;
    }
    char script[1024];
    snprintf(script, sizeof(script),
        "var contents = io_read_file(\"%s\");\n"
        "if (contents == nil) {\n"
        "  print \"unexpectedly nil\";\n"
        "} else if (contents == \"\") {\n"
        "  print \"empty\";\n"
        "} else {\n"
        "  print \"unexpectedly non-empty\";\n"
        "}\n",
        path);
    int exitCode;
    char *out = runClox(script, &exitCode);
    if (exitCode != 0) {
        fail("stdlib/io-read-file-empty: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "empty\n")) {
        fail("stdlib/io-read-file-empty: expected 'empty' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
    unlink(path);
}

static void test_io_read_file_wrong_args(void) {
    /* Arity error. */
    int exitCode;
    char *out = runClox("io_read_file();\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/io-read-file-wrong-args: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);
}

static void test_io_read_file_wrong_type(void) {
    /* Type error: path must be string. */
    int exitCode;
    char *out = runClox("io_read_file(42);\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/io-read-file-wrong-type: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);
}

/* --- io_write_file tests --- */

static void test_io_write_file_new(void) {
    /* Write to a new file, then read it back to verify contents. */
    char path[256];
    buildTmpPath(path, sizeof(path), "write_new.txt");
    unlink(path);  /* ensure it doesn't exist */

    char script[1024];
    snprintf(script, sizeof(script),
        "io_write_file(\"%s\", \"written by clox\\nsecond line\\n\");\n",
        path);
    int exitCode;
    char *out = runClox(script, &exitCode);
    if (exitCode != 0) {
        fail("stdlib/io-write-file-new: write phase expected exit 0, got %d (output: %s)", exitCode, out);
        free(out);
        unlink(path);
        return;
    }
    free(out);

    /* Now verify the file exists and has the right contents via clox itself. */
    char verify[1024];
    snprintf(verify, sizeof(verify),
        "if (io_file_exists(\"%s\")) {\n"
        "  var c = io_read_file(\"%s\");\n"
        "  print c;\n"
        "} else {\n"
        "  print \"file was not created\";\n"
        "}\n",
        path, path);
    out = runClox(verify, &exitCode);
    if (exitCode != 0) {
        fail("stdlib/io-write-file-new: verify phase expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "written by clox") || !contains(out, "second line")) {
        fail("stdlib/io-write-file-new: expected written contents in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
    unlink(path);
}

static void test_io_write_file_overwrite(void) {
    /* Pre-seed a file with old contents, write new contents, verify overwrite. */
    char path[256];
    buildTmpPath(path, sizeof(path), "write_overwrite.txt");
    if (writeFileToDisk(path, "OLD OLD OLD") != 0) {
        fail("stdlib/io-write-file-overwrite: could not seed test file");
        return;
    }
    char script[1024];
    snprintf(script, sizeof(script),
        "io_write_file(\"%s\", \"NEW\");\n"
        "print(io_read_file(\"%s\"));\n",
        path, path);
    int exitCode;
    char *out = runClox(script, &exitCode);
    if (exitCode != 0) {
        fail("stdlib/io-write-file-overwrite: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "NEW\n") || contains(out, "OLD")) {
        fail("stdlib/io-write-file-overwrite: expected NEW without OLD, got '%s'", out);
    } else {
        pass();
    }
    free(out);
    unlink(path);
}

static void test_io_write_file_empty(void) {
    /* Writing empty contents is allowed and creates an empty file. */
    char path[256];
    buildTmpPath(path, sizeof(path), "write_empty.txt");
    unlink(path);
    char script[1024];
    snprintf(script, sizeof(script),
        "io_write_file(\"%s\", \"\");\n"
        "if (io_file_exists(\"%s\")) {\n"
        "  print \"exists\";\n"
        "} else {\n"
        "  print \"missing\";\n"
        "}\n",
        path, path);
    int exitCode;
    char *out = runClox(script, &exitCode);
    if (exitCode != 0) {
        fail("stdlib/io-write-file-empty: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "exists\n")) {
        fail("stdlib/io-write-file-empty: expected file to exist, got '%s'", out);
    } else {
        pass();
    }
    free(out);
    unlink(path);
}

static void test_io_write_file_wrong_args(void) {
    /* Arity error. */
    int exitCode;
    char *out = runClox("io_write_file(\"/tmp/x\");\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/io-write-file-wrong-args: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);
}

static void test_io_write_file_wrong_type(void) {
    /* Type error: path must be string, contents must be string. */
    int exitCode;
    char *out = runClox("io_write_file(42, \"data\");\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/io-write-file-wrong-type-path: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);
}

/* --- io_file_exists tests --- */

static void test_io_file_exists_true(void) {
    /* Pre-seed a file, then check. */
    char path[256];
    buildTmpPath(path, sizeof(path), "exists_true.txt");
    if (writeFileToDisk(path, "x") != 0) {
        fail("stdlib/io-file-exists-true: could not seed test file");
        return;
    }
    char script[1024];
    snprintf(script, sizeof(script),
        "print(io_file_exists(\"%s\"));\n",
        path);
    int exitCode;
    char *out = runClox(script, &exitCode);
    if (exitCode != 0) {
        fail("stdlib/io-file-exists-true: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "true\n")) {
        fail("stdlib/io-file-exists-true: expected 'true' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
    unlink(path);
}

static void test_io_file_exists_false(void) {
    /* Check a non-existent file. */
    int exitCode;
    char *out = runClox(
        "print(io_file_exists(\"/tmp/clox_s14_does_not_exist_xyz_67890.txt\"));\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/io-file-exists-false: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "false\n")) {
        fail("stdlib/io-file-exists-false: expected 'false' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_io_file_exists_wrong_args(void) {
    /* Arity error. */
    int exitCode;
    char *out = runClox("io_file_exists();\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/io-file-exists-wrong-args: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);
}

static void test_io_file_exists_wrong_type(void) {
    /* Type error: path must be string. */
    int exitCode;
    char *out = runClox("io_file_exists(42);\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/io-file-exists-wrong-type: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);
}

/* --- GC stress test for file I/O --- */

static void test_io_file_gc_stress(void) {
    /* 200 iterations of write+read+exists on a single file. Each
     * iteration allocates a string from io_read_file. Valgrind must
     * verify all allocs balance. */
    char path[256];
    buildTmpPath(path, sizeof(path), "gc_stress.txt");
    unlink(path);

    char script[4096];
    snprintf(script, sizeof(script),
        "var path = \"%s\";\n"
        "var sink = [];\n"
        "var i = 0;\n"
        "while (i < 200) {\n"
        "  io_write_file(path, \"stress test content\");\n"
        "  var contents = io_read_file(path);\n"
        "  array_push(sink, contents);\n"
        "  i = i + 1;\n"
        "}\n"
        "print(array_length(sink));\n"
        "print(sink[0]);\n",
        path);

    int exitCode;
    char *out = runClox(script, &exitCode);
    if (exitCode != 0) {
        fail("stdlib/io-file-gc-stress: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "200\n") || !contains(out, "stress test content\n")) {
        fail("stdlib/io-file-gc-stress: expected 200 and stress content in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
    unlink(path);
}

/* --- Stage 16 tests: io_read_lines / io_write_lines ---
 *
 * The streaming I/O pattern: read a file as an array of lines, or
 * write an array of lines to a file. The new natives compose with
 * the existing string and array operations to make file handling
 * a first-class clox idiom.
 *
 * The tests use fixed /tmp paths and clean up after themselves, same
 * pattern as the Stage 14 file I/O tests. */

static void test_io_read_lines_basic(void) {
    /* 3-line file with trailing newline. The trailing empty piece
     * is dropped (lines are content, not separators). Result: 3
     * elements: "alpha", "beta", "gamma". */
    char path[256];
    buildTmpPath(path, sizeof(path), "read_lines_basic.txt");
    if (writeFileToDisk(path, "alpha\nbeta\ngamma\n") != 0) {
        fail("stdlib/io-read-lines-basic: could not seed test file");
        return;
    }
    char script[1024];
    snprintf(script, sizeof(script),
        "var lines = io_read_lines(\"%s\");\n"
        "print(array_length(lines));\n"
        "print(lines[0]);\n"
        "print(lines[1]);\n"
        "print(lines[2]);\n",
        path);
    int exitCode;
    char *out = runClox(script, &exitCode);
    if (exitCode != 0) {
        fail("stdlib/io-read-lines-basic: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "3\n") || !contains(out, "alpha\n") ||
               !contains(out, "beta\n") || !contains(out, "gamma\n")) {
        fail("stdlib/io-read-lines-basic: expected 3, alpha, beta, gamma in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
    unlink(path);
}

static void test_io_read_lines_no_trailing_newline(void) {
    /* File without trailing newline. Should still return all 3 lines. */
    char path[256];
    buildTmpPath(path, sizeof(path), "read_lines_no_trail.txt");
    if (writeFileToDisk(path, "first\nsecond\nthird") != 0) {
        fail("stdlib/io-read-lines-no-trail: could not seed test file");
        return;
    }
    char script[1024];
    snprintf(script, sizeof(script),
        "var lines = io_read_lines(\"%s\");\n"
        "print(array_length(lines));\n"
        "print(lines[0]);\n"
        "print(lines[2]);\n",
        path);
    int exitCode;
    char *out = runClox(script, &exitCode);
    if (exitCode != 0) {
        fail("stdlib/io-read-lines-no-trail: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "3\n") || !contains(out, "first\n") ||
               !contains(out, "third\n")) {
        fail("stdlib/io-read-lines-no-trail: expected 3, first, third in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
    unlink(path);
}

static void test_io_read_lines_empty_file(void) {
    /* Empty file -> empty array. */
    char path[256];
    buildTmpPath(path, sizeof(path), "read_lines_empty.txt");
    if (writeFileToDisk(path, "") != 0) {
        fail("stdlib/io-read-lines-empty: could not seed test file");
        return;
    }
    char script[1024];
    snprintf(script, sizeof(script),
        "var lines = io_read_lines(\"%s\");\n"
        "print(array_length(lines));\n",
        path);
    int exitCode;
    char *out = runClox(script, &exitCode);
    if (exitCode != 0) {
        fail("stdlib/io-read-lines-empty: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "0\n")) {
        fail("stdlib/io-read-lines-empty: expected 0 in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
    unlink(path);
}

static void test_io_read_lines_nonexistent(void) {
    /* File doesn't exist -> nil. */
    int exitCode;
    char *out = runClox(
        "var lines = io_read_lines(\"/tmp/clox_s16_does_not_exist_xyz_99999.txt\");\n"
        "if (lines == nil) {\n"
        "  print \"absent\";\n"
        "} else {\n"
        "  print \"unexpectedly got lines\";\n"
        "}\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/io-read-lines-nonexistent: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "absent\n")) {
        fail("stdlib/io-read-lines-nonexistent: expected 'absent' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_io_read_lines_single_line(void) {
    /* Single line, no newline. Result: array of 1. */
    char path[256];
    buildTmpPath(path, sizeof(path), "read_lines_single.txt");
    if (writeFileToDisk(path, "just one line") != 0) {
        fail("stdlib/io-read-lines-single: could not seed test file");
        return;
    }
    char script[1024];
    snprintf(script, sizeof(script),
        "var lines = io_read_lines(\"%s\");\n"
        "print(array_length(lines));\n"
        "print(lines[0]);\n",
        path);
    int exitCode;
    char *out = runClox(script, &exitCode);
    if (exitCode != 0) {
        fail("stdlib/io-read-lines-single: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "1\n") || !contains(out, "just one line\n")) {
        fail("stdlib/io-read-lines-single: expected 1 and 'just one line' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
    unlink(path);
}

static void test_io_read_lines_wrong_args(void) {
    /* Arity error. */
    int exitCode;
    char *out = runClox("io_read_lines();\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/io-read-lines-wrong-args: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);
}

static void test_io_read_lines_wrong_type(void) {
    /* Type error: path must be string. */
    int exitCode;
    char *out = runClox("io_read_lines(42);\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/io-read-lines-wrong-type: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);
}

static void test_io_write_lines_new(void) {
    /* Write a new file from an array of lines. Then read it back
     * via io_read_file and verify the content. */
    char path[256];
    buildTmpPath(path, sizeof(path), "write_lines_new.txt");
    unlink(path);
    char script[1024];
    snprintf(script, sizeof(script),
        "io_write_lines(\"%s\", [\"first\", \"second\", \"third\"]);\n"
        "print(io_read_file(\"%s\"));\n",
        path, path);
    int exitCode;
    char *out = runClox(script, &exitCode);
    if (exitCode != 0) {
        fail("stdlib/io-write-lines-new: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "first\nsecond\nthird\n")) {
        fail("stdlib/io-write-lines-new: expected 'first\\nsecond\\nthird\\n' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
    unlink(path);
}

static void test_io_write_lines_overwrite(void) {
    /* Pre-seed with old content, overwrite with new lines, verify. */
    char path[256];
    buildTmpPath(path, sizeof(path), "write_lines_overwrite.txt");
    if (writeFileToDisk(path, "OLD OLD OLD") != 0) {
        fail("stdlib/io-write-lines-overwrite: could not seed test file");
        return;
    }
    char script[1024];
    snprintf(script, sizeof(script),
        "io_write_lines(\"%s\", [\"new1\", \"new2\"]);\n"
        "print(io_read_file(\"%s\"));\n",
        path, path);
    int exitCode;
    char *out = runClox(script, &exitCode);
    if (exitCode != 0) {
        fail("stdlib/io-write-lines-overwrite: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "new1\nnew2\n") || contains(out, "OLD")) {
        fail("stdlib/io-write-lines-overwrite: expected new content without OLD, got '%s'", out);
    } else {
        pass();
    }
    free(out);
    unlink(path);
}

static void test_io_write_lines_empty_array(void) {
    /* Empty array -> empty file. */
    char path[256];
    buildTmpPath(path, sizeof(path), "write_lines_empty.txt");
    unlink(path);
    char script[1024];
    snprintf(script, sizeof(script),
        "io_write_lines(\"%s\", []);\n"
        "if (io_file_exists(\"%s\")) {\n"
        "  var c = io_read_file(\"%s\");\n"
        "  print(\"empty:\");\n"
        "  print string_length(c);\n"
        "} else {\n"
        "  print \"missing\";\n"
        "}\n",
        path, path, path);
    int exitCode;
    char *out = runClox(script, &exitCode);
    if (exitCode != 0) {
        fail("stdlib/io-write-lines-empty: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "empty:\n0\n")) {
        fail("stdlib/io-write-lines-empty: expected 'empty:\\n0' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
    unlink(path);
}

static void test_io_write_lines_single_element(void) {
    /* Single-element array -> file with that line and a trailing newline. */
    char path[256];
    buildTmpPath(path, sizeof(path), "write_lines_single.txt");
    unlink(path);
    char script[1024];
    snprintf(script, sizeof(script),
        "io_write_lines(\"%s\", [\"alone\"]);\n"
        "print(io_read_file(\"%s\"));\n",
        path, path);
    int exitCode;
    char *out = runClox(script, &exitCode);
    if (exitCode != 0) {
        fail("stdlib/io-write-lines-single: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "alone\n")) {
        fail("stdlib/io-write-lines-single: expected 'alone\\n' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
    unlink(path);
}

static void test_io_write_lines_wrong_args(void) {
    /* Arity error. */
    int exitCode;
    char *out = runClox("io_write_lines(\"/tmp/x\");\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/io-write-lines-wrong-args: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);
}

static void test_io_write_lines_wrong_type(void) {
    /* Type error: path must be string, arr must be array. */
    int exitCode;
    char *out = runClox("io_write_lines(42, []);\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/io-write-lines-wrong-type-path: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);

    exitCode = -1;
    out = runClox("io_write_lines(\"/tmp/x\", \"not an array\");\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/io-write-lines-wrong-type-arr: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);
}

static void test_io_read_write_lines_round_trip(void) {
    /* Write an array of lines, read it back as lines, verify
     * identity (modulo the trailing newline behavior). */
    char path[256];
    buildTmpPath(path, sizeof(path), "round_trip.txt");
    unlink(path);
    char script[2048];
    snprintf(script, sizeof(script),
        "var original = [\"one\", \"two\", \"three\", \"four\"];\n"
        "io_write_lines(\"%s\", original);\n"
        "var readback = io_read_lines(\"%s\");\n"
        "print(array_length(readback));\n"
        "print(readback[0]);\n"
        "print(readback[3]);\n"
        "print(readback[0] == original[0]);\n"
        "print(readback[3] == original[3]);\n",
        path, path);
    int exitCode;
    char *out = runClox(script, &exitCode);
    if (exitCode != 0) {
        fail("stdlib/io-round-trip: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "4\n") || !contains(out, "one\n") || !contains(out, "four\n") ||
               !contains(out, "true\n")) {
        fail("stdlib/io-round-trip: expected 4, one, four, true in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
    unlink(path);
}

static void test_io_lines_gc_stress(void) {
    /* 200 iterations of read_lines on a file with 3 lines. The
     * returned array grows across iterations, forcing collection.
     * Valgrind must verify all allocs balance. */
    char path[256];
    buildTmpPath(path, sizeof(path), "lines_gc_stress.txt");
    if (writeFileToDisk(path, "line1\nline2\nline3\n") != 0) {
        fail("stdlib/io-lines-gc-stress: could not seed test file");
        return;
    }
    char script[2048];
    snprintf(script, sizeof(script),
        "var path = \"%s\";\n"
        "var sink = [];\n"
        "var i = 0;\n"
        "while (i < 200) {\n"
        "  var lines = io_read_lines(path);\n"
        "  array_push(sink, lines);\n"
        "  i = i + 1;\n"
        "}\n"
        "print(array_length(sink));\n"
        "print(array_length(sink[0]));\n"
        "print(sink[0][0]);\n",
        path);
    int exitCode;
    char *out = runClox(script, &exitCode);
    if (exitCode != 0) {
        fail("stdlib/io-lines-gc-stress: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "200\n") || !contains(out, "3\n") || !contains(out, "line1\n")) {
        fail("stdlib/io-lines-gc-stress: expected 200, 3, line1 in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
    unlink(path);
}

/* --- Stage 17 tests: string(n) for number-to-string conversion.
 *
 * The new native closes the "user cannot print a number in a
 * sentence" gap from Stage 15's composability tests. The contract:
 *   string(n) -> string, where n is a number.
 *   - Integers: "42", "0", "-7"
 *   - Floats: round-trip representation, no trailing zeros
 *     ("3.14", not "3.140000")
 *   - 5.0 -> "5" (integer-valued floats drop the decimal)
 *   - Wrong arity / wrong type: runtime error.
 *
 * Format spec: we use "%.14g" which is the round-trip-precision
 * format for double-precision floats (it produces the shortest
 * string that round-trips back to the same double). */

static void test_string_int_positive(void) {
    int exitCode;
    char *out = runClox("print string(42);\n", &exitCode);
    if (exitCode != 0) {
        fail("stdlib/string-int-positive: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "42\n")) {
        fail("stdlib/string-int-positive: expected '42' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_string_int_zero(void) {
    int exitCode;
    char *out = runClox("print string(0);\n", &exitCode);
    if (exitCode != 0) {
        fail("stdlib/string-int-zero: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "0\n")) {
        fail("stdlib/string-int-zero: expected '0' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_string_int_negative(void) {
    int exitCode;
    char *out = runClox("print string(-7);\n", &exitCode);
    if (exitCode != 0) {
        fail("stdlib/string-int-negative: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "-7\n")) {
        fail("stdlib/string-int-negative: expected '-7' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_string_float(void) {
    /* 3.14 -> "3.14". %.14g produces the shortest round-trip
     * representation, which for 3.14 is "3.14" (not "3.140000"
     * or "3.1399999999999999"). */
    int exitCode;
    char *out = runClox("print string(3.14);\n", &exitCode);
    if (exitCode != 0) {
        fail("stdlib/string-float: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "3.14\n")) {
        fail("stdlib/string-float: expected '3.14' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_string_float_integer_valued(void) {
    /* 5.0 -> "5", not "5.0" or "5.000000". %.14g drops trailing
     * zeros after the decimal point. */
    int exitCode;
    char *out = runClox("print string(5.0);\n", &exitCode);
    if (exitCode != 0) {
        fail("stdlib/string-float-integer-valued: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "5\n")) {
        fail("stdlib/string-float-integer-valued: expected '5' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_string_wrong_args(void) {
    /* 0 args: arity error. */
    int exitCode;
    char *out = runClox("string();\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/string-wrong-args-zero: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);

    /* 2 args: arity error. */
    exitCode = -1;
    out = runClox("string(1, 2);\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/string-wrong-args-two: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);
}

static void test_string_wrong_type(void) {
    /* String argument is a runtime error. */
    int exitCode;
    char *out = runClox("string(\"hello\");\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/string-wrong-type: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);
}

static void test_string_concat_pattern(void) {
    /* The whole point of Stage 17: print a number in a sentence.
     * Before Stage 17 this required separate print calls; now it's
     * a single concatenation. */
    int exitCode;
    char *out = runClox(
        "var age = 42;\n"
        "var pi = 3.14;\n"
        "print(\"I am \" + string(age) + \" years old\");\n"
        "print(\"Pi is roughly \" + string(pi));\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/string-concat-pattern: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "I am 42 years old\n") || !contains(out, "Pi is roughly 3.14\n")) {
        fail("stdlib/string-concat-pattern: expected 'I am 42 years old' and 'Pi is roughly 3.14' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_string_gc_stress(void) {
    /* 1000 calls to string(i) for i in 0..1000. Each call allocates
     * a fresh ObjString. Valgrind must verify all allocs balance. */
    int exitCode;
    char *out = runClox(
        "var sink = [];\n"
        "var i = 0;\n"
        "while (i < 1000) {\n"
        "  array_push(sink, string(i));\n"
        "  i = i + 1;\n"
        "}\n"
        "print(array_length(sink));\n"
        "print(sink[0]);\n"
        "print(sink[999]);\n"
        "print(sink[500]);\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/string-gc-stress: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "1000\n") || !contains(out, "0\n") ||
               !contains(out, "999\n") || !contains(out, "500\n")) {
        fail("stdlib/string-gc-stress: expected 1000, 0, 999, 500 in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

/* --- Stage 19 tests: array_reverse(arr) -> arr (in place) ---
 *
 * array_reverse mutates the input array in place, reversing the
 * order of its elements, and returns the same array (so calls
 * can chain). This closes the third of the four language gaps
 * from Stage 15's composability tests (the hand-rolled reverse
 * loop). */

static void test_array_reverse_basic(void) {
    /* [1, 2, 3] reversed is [3, 2, 1]. */
    int exitCode;
    char *out = runClox(
        "var a = [1, 2, 3];\n"
        "array_reverse(a);\n"
        "print(a[0]);\n"
        "print(a[1]);\n"
        "print(a[2]);\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-reverse-basic: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "3\n2\n1\n")) {
        fail("stdlib/array-reverse-basic: expected '3\\n2\\n1\\n' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_reverse_returns_self(void) {
    /* The returned value is the same array object. */
    int exitCode;
    char *out = runClox(
        "var a = [10, 20, 30];\n"
        "var b = array_reverse(a);\n"
        "print(b == a);\n"
        "print(b[0]);\n"
        "print(b[1]);\n"
        "print(b[2]);\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-reverse-returns-self: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "true\n30\n20\n10\n")) {
        fail("stdlib/array-reverse-returns-self: expected 'true\\n30\\n20\\n10\\n' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_reverse_empty(void) {
    /* Reversing an empty array is a no-op. Length stays 0. */
    int exitCode;
    char *out = runClox(
        "var a = [];\n"
        "array_reverse(a);\n"
        "print(array_length(a));\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-reverse-empty: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "0\n")) {
        fail("stdlib/array-reverse-empty: expected '0' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_reverse_single(void) {
    /* Reversing a 1-element array leaves it unchanged. */
    int exitCode;
    char *out = runClox(
        "var a = [42];\n"
        "array_reverse(a);\n"
        "print(a[0]);\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-reverse-single: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "42\n")) {
        fail("stdlib/array-reverse-single: expected '42' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_reverse_two(void) {
    /* [1, 2] -> [2, 1]. The smallest non-trivial case. */
    int exitCode;
    char *out = runClox(
        "var a = [1, 2];\n"
        "array_reverse(a);\n"
        "print(a[0]);\n"
        "print(a[1]);\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-reverse-two: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "2\n1\n")) {
        fail("stdlib/array-reverse-two: expected '2\\n1\\n' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_reverse_even_length(void) {
    /* [1, 2, 3, 4] -> [4, 3, 2, 1]. Exercises the full swap loop. */
    int exitCode;
    char *out = runClox(
        "var a = [1, 2, 3, 4];\n"
        "array_reverse(a);\n"
        "print(a[0]);\n"
        "print(a[1]);\n"
        "print(a[2]);\n"
        "print(a[3]);\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-reverse-even: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "4\n3\n2\n1\n")) {
        fail("stdlib/array-reverse-even: expected '4\\n3\\n2\\n1\\n' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_reverse_strings(void) {
    /* array_reverse works on arrays of strings, not just numbers. */
    int exitCode;
    char *out = runClox(
        "var a = [\"a\", \"b\", \"c\"];\n"
        "array_reverse(a);\n"
        "print(a[0]);\n"
        "print(a[1]);\n"
        "print(a[2]);\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-reverse-strings: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "c\nb\na\n")) {
        fail("stdlib/array-reverse-strings: expected 'c\\nb\\na\\n' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_reverse_then_push(void) {
    /* After reversing, push works as expected. */
    int exitCode;
    char *out = runClox(
        "var a = [1, 2, 3];\n"
        "array_reverse(a);\n"
        "array_push(a, 99);\n"
        "print(a[0]);\n"
        "print(a[1]);\n"
        "print(a[2]);\n"
        "print(a[3]);\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-reverse-then-push: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "3\n2\n1\n99\n")) {
        fail("stdlib/array-reverse-then-push: expected '3\\n2\\n1\\n99\\n' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_reverse_twice(void) {
    /* Reversing twice returns to the original order. */
    int exitCode;
    char *out = runClox(
        "var a = [1, 2, 3, 4, 5];\n"
        "array_reverse(a);\n"
        "array_reverse(a);\n"
        "print(a[0]);\n"
        "print(a[1]);\n"
        "print(a[2]);\n"
        "print(a[3]);\n"
        "print(a[4]);\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-reverse-twice: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "1\n2\n3\n4\n5\n")) {
        fail("stdlib/array-reverse-twice: expected '1\\n2\\n3\\n4\\n5\\n' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_reverse_wrong_arg_count(void) {
    /* array_reverse() with no args is a runtime error. */
    int exitCode;
    char *out = runClox("array_reverse();\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/array-reverse-wrong-args: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);
}

static void test_array_reverse_wrong_type(void) {
    /* array_reverse("not an array") is a runtime error. */
    int exitCode;
    char *out = runClox("array_reverse(\"hello\");\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/array-reverse-wrong-type: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);
}

/* --- Stage 20 tests: array_push now returns the new length ---
 *
 * Convention refinement: array_push used to return nil, forcing
 * the user to read the new length via a separate array_length()
 * call. The new convention is "return the natural value": for
 * array_push, the natural return value is the new length. This
 * is consistent with the Stage 19 reverse-style mutator return
 * pattern (return a useful value, not nil).
 *
 * Existing tests that call array_push without using the return
 * value still pass — the return value is ignored if the call
 * appears as a statement. */

static void test_array_push_returns_new_length(void) {
    /* array_push returns the new length, not nil. */
    int exitCode;
    char *out = runClox(
        "var a = [1, 2, 3];\n"
        "var n = array_push(a, 4);\n"
        "print(n);\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-push-returns-new-length: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "4\n")) {
        fail("stdlib/array-push-returns-new-length: expected '4' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_push_returns_length_growing(void) {
    /* Each push returns the new (larger) length. */
    int exitCode;
    char *out = runClox(
        "var a = [];\n"
        "print(array_push(a, \"a\"));\n"   /* 1 */
        "print(array_push(a, \"b\"));\n"   /* 2 */
        "print(array_push(a, \"c\"));\n",  /* 3 */
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-push-returns-length-growing: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "1\n2\n3\n")) {
        fail("stdlib/array-push-returns-length-growing: expected '1\\n2\\n3\\n' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_push_return_value_is_number(void) {
    /* The return value is a number, not an array. The user can
     * do arithmetic on it. */
    int exitCode;
    char *out = runClox(
        "var a = [1, 2, 3];\n"
        "var n = array_push(a, 4);\n"
        "print(n + 10);\n"        /* 14, not 4 (which would mean
                                    * the return was the new element) */
        "print(n - 3);\n",        /* 1, which is the old length */
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-push-return-value-is-number: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "14\n1\n")) {
        fail("stdlib/array-push-return-value-is-number: expected '14\\n1\\n' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_push_chained_returns(void) {
    /* Chained: each push returns the new length, so the next
     * push can use it. */
    int exitCode;
    char *out = runClox(
        "var a = [];\n"
        "var n = 0;\n"
        "n = array_push(a, 1);\n"
        "n = array_push(a, 2);\n"
        "n = array_push(a, 3);\n"
        "print(n);\n"               /* 3 */
        "print(array_length(a));\n", /* 3, both agree */
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-push-chained-returns: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "3\n3\n")) {
        fail("stdlib/array-push-chained-returns: expected '3\\n3\\n' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

/* --- Stage 21 tests: string_repeat --- */

static void test_string_repeat_basic(void) {
    /* string_repeat(s, n) -> string. Concatenate s with itself n times. */
    int exitCode;
    char *out = runClox(
        "print string_repeat(\"ha\", 3);\n"          /* "hahaha" */
        "print string_repeat(\"x\", 5);\n"           /* "xxxxx" */
        "print string_repeat(\"abc\", 1);\n"         /* "abc" */
        "print string_repeat(\"hello\", 0);\n",      /* "" */
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/repeat: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "hahaha\n")
            || !contains(out, "xxxxx\n")
            || !contains(out, "abc\n")) {
        fail("stdlib/repeat: expected 'hahaha', 'xxxxx', 'abc' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_string_repeat_zero(void) {
    /* n = 0 -> empty string. The "repeat zero times" idiom. */
    int exitCode;
    char *out = runClox(
        "var r = string_repeat(\"anything\", 0);\n"
        "print string_length(r);\n"
        "print(r);\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/repeat-zero: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "0\n")) {
        fail("stdlib/repeat-zero: expected '0\\n' (empty length) in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_string_repeat_negative_errors(void) {
    /* Negative n -> runtime error. Matches string_substring's discipline:
     * bad numeric input is a runtime error, not silent. */
    int exitCode;
    char *out = runClox("print string_repeat(\"x\", -1);\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/repeat-negative: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);
}

static void test_string_repeat_wrong_type(void) {
    /* Non-string first arg -> runtime error. */
    int exitCode;
    char *out = runClox("print string_repeat(42, 3);\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/repeat-wrong-type: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);
}

static void test_string_repeat_wrong_arg_count(void) {
    /* Wrong arity (0 args) -> runtime error. */
    int exitCode;
    char *out = runClox("print string_repeat();\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/repeat-wrong-arg-count: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);
}

/* --- Stage 22 tests: string_pad_start --- */

static void test_string_pad_start_basic(void) {
    /* string_pad_start(s, width, fill) -> string. Pad s on the left
     * with copies of fill until the result is at least width chars.
     * Default Python convention: s already wider than width is unchanged. */
    int exitCode;
    char *out = runClox(
        "print string_pad_start(\"5\", 3, \"0\");\n"   /* "005" */
        "print string_pad_start(\"42\", 5, \"0\");\n"  /* "00042" */
        "print string_pad_start(\"x\", 4, \"ab\");\n"  /* "abax" */
        "print string_pad_start(\"hi\", 6, \"-=\");\n",/* "-=-=hi" (width 6, fill "-=" repeats 2x = 4 chars + "hi" = 6) */
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/pad-start: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "005\n")
            || !contains(out, "00042\n")
            || !contains(out, "abax\n")
            || !contains(out, "-=-=hi\n")) {
        fail("stdlib/pad-start: expected padded strings in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_string_pad_start_already_wide(void) {
    /* s already >= width -> return s unchanged (no truncation, no error). */
    int exitCode;
    char *out = runClox(
        "print string_pad_start(\"hello\", 3, \"0\");\n"   /* "hello" */
        "print string_pad_start(\"abc\", 3, \"x\");\n",    /* "abc" (exact) */
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/pad-start-wide: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "hello\n")
            || !contains(out, "abc\n")
            || contains(out, "00hello")
            || contains(out, "xabc")) {
        fail("stdlib/pad-start-wide: expected s unchanged when wide enough, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_string_pad_start_empty_s(void) {
    /* Empty s -> result is just fill repeated to width. */
    int exitCode;
    char *out = runClox(
        "var r = string_pad_start(\"\", 4, \"x\");\n"
        "print string_length(r);\n"
        "print(r);\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/pad-start-empty-s: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "4\n")
            || !contains(out, "xxxx\n")) {
        fail("stdlib/pad-start-empty-s: expected 'xxxx' length 4, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_string_pad_start_width_zero(void) {
    /* width = 0 -> s unchanged. The "pad to zero" idiom. */
    int exitCode;
    char *out = runClox(
        "var r = string_pad_start(\"hi\", 0, \"x\");\n"
        "print(r);\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/pad-start-width-zero: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "hi\n")
            || contains(out, "xhi")) {
        fail("stdlib/pad-start-width-zero: expected 'hi' unchanged, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_string_pad_start_negative_errors(void) {
    /* Negative width -> runtime error (matches string_repeat /
     * string_substring's discipline: bad numeric input is a runtime
     * error, not silent). */
    int exitCode;
    char *out = runClox("print string_pad_start(\"x\", -1, \"0\");\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/pad-start-negative: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);
}

static void test_string_pad_start_empty_fill_errors(void) {
    /* Empty fill -> runtime error. Padding with nothing is nonsensical;
     * if you wanted to truncate, use string_substring. */
    int exitCode;
    char *out = runClox("print string_pad_start(\"x\", 5, \"\");\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/pad-start-empty-fill: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);
}

static void test_string_pad_start_wrong_type(void) {
    /* Non-string first arg -> runtime error. */
    int exitCode;
    char *out = runClox("print string_pad_start(42, 5, \"0\");\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/pad-start-wrong-type-s: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);
}

static void test_string_pad_start_wrong_arg_count(void) {
    /* Wrong arity (1 arg) -> runtime error. */
    int exitCode;
    char *out = runClox("print string_pad_start(\"x\");\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/pad-start-wrong-arg-count: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);
}

/* --- Stage 23: string_pad_end --- */
/* string_pad_end(s, width, fill) -> string. Mirror of string_pad_start:
 * pad s on the RIGHT with copies of fill until the result is at least
 * width characters. JavaScript's String.prototype.padEnd semantics
 * (Python's str.ljust doesn't support multi-char fill, so JS is the
 * canonical reference). Same error contract as string_pad_start:
 * negative width and empty fill are runtime errors; s->length >=
 * width returns s unchanged. */

static void test_string_pad_end_basic(void) {
    /* Basic case: pad to 6 with "-=", expect "abc-=-" (6 chars). The
     * JS reference returns "abc-=-", not "abc=-=-" (7 chars). The
     * fill is truncated to fit the target width. */
    int exitCode;
    char *out = runClox(
        "var r = string_pad_end(\"abc\", 6, \"-=\");\n"
        "print string_length(r);\n"
        "print(r);\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/pad-end-basic: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "6\n")
            || !contains(out, "abc-=-\n")
            || contains(out, "abc=-=-")) {
        fail("stdlib/pad-end-basic: expected 'abc-=-' length 6, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_string_pad_end_already_wide(void) {
    /* s->length >= width -> s unchanged (JS padEnd convention:
     * never truncate, never error on "already wide enough"). */
    int exitCode;
    char *out = runClox(
        "print string_pad_end(\"hello\", 3, \"0\");\n"   /* "hello" */
        "print string_pad_end(\"abc\", 3, \"x\");\n",    /* "abc" (exact) */
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/pad-end-wide: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "hello\n")
            || !contains(out, "abc\n")
            || contains(out, "hello000")
            || contains(out, "abcxxx")) {
        fail("stdlib/pad-end-wide: expected s unchanged when wide enough, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_string_pad_end_empty_s(void) {
    /* Empty s -> result is just fill repeated to width. */
    int exitCode;
    char *out = runClox(
        "var r = string_pad_end(\"\", 4, \"x\");\n"
        "print string_length(r);\n"
        "print(r);\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/pad-end-empty-s: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "4\n")
            || !contains(out, "xxxx\n")) {
        fail("stdlib/pad-end-empty-s: expected 'xxxx' length 4, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_string_pad_end_width_zero(void) {
    /* width = 0 -> s unchanged. */
    int exitCode;
    char *out = runClox(
        "var r = string_pad_end(\"hi\", 0, \"x\");\n"
        "print(r);\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/pad-end-width-zero: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "hi\n")
            || contains(out, "hix")) {
        fail("stdlib/pad-end-width-zero: expected 'hi' unchanged, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_string_pad_end_negative_errors(void) {
    /* Negative width -> runtime error (matches string_pad_start /
     * string_repeat / string_substring's discipline). */
    int exitCode;
    char *out = runClox("print string_pad_end(\"x\", -1, \"0\");\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/pad-end-negative: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);
}

static void test_string_pad_end_empty_fill_errors(void) {
    /* Empty fill -> runtime error (matches string_pad_start). */
    int exitCode;
    char *out = runClox("print string_pad_end(\"x\", 5, \"\");\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/pad-end-empty-fill: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);
}

static void test_string_pad_end_wrong_type(void) {
    /* Non-string first arg -> runtime error. */
    int exitCode;
    char *out = runClox("print string_pad_end(42, 5, \"0\");\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/pad-end-wrong-type-s: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);
}

static void test_string_pad_end_wrong_arg_count(void) {
    /* Wrong arity (1 arg) -> runtime error. */
    int exitCode;
    char *out = runClox("print string_pad_end(\"x\");\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/pad-end-wrong-arg-count: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);
}

/* --- Stage 24: string_to_number --- */
/* string_to_number(s) -> number. The inverse of Stage 17's string(n).
 * Parses a decimal number from a string. JavaScript's parseFloat
 * semantics (with strict-error-on-NaN, strict-error-on-overflow):
 * "42" -> 42.0, "3.14" -> 3.14, "-7" -> -7.0, "0" -> 0, "  42  "
 * -> 42 (leading/trailing whitespace trimmed by strtod), "3.14e2"
 * -> 314 (scientific notation accepted). Errors: empty string,
 * non-numeric, overflow to Infinity, no characters consumed. */

static void test_string_to_number_int_positive(void) {
    /* "42" -> 42. Adding 1 to a parsed int gives 43 — the round-trip
     * idiom working end-to-end. */
    int exitCode;
    char *out = runClox(
        "var n = string_to_number(\"42\");\n"
        "print n + 1;\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/to-number-int-positive: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "43\n")) {
        fail("stdlib/to-number-int-positive: expected '43' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_string_to_number_int_negative(void) {
    /* "-7" -> -7. Negative signs parse correctly. */
    int exitCode;
    char *out = runClox(
        "var n = string_to_number(\"-7\");\n"
        "print n * 2;\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/to-number-int-negative: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "-14\n")) {
        fail("stdlib/to-number-int-negative: expected '-14' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_string_to_number_float(void) {
    /* "3.14" -> 3.14. Floats parse with their decimal part preserved.
     * 3.14 + 1.0 = 4.14 (no trailing zeros, %.14g short-form). */
    int exitCode;
    char *out = runClox(
        "var n = string_to_number(\"3.14\");\n"
        "print n + 1.0;\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/to-number-float: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "4.14\n")) {
        fail("stdlib/to-number-float: expected '4.14\\n' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_string_to_number_scientific(void) {
    /* "3.14e2" -> 314. Scientific notation parses correctly. */
    int exitCode;
    char *out = runClox(
        "print string_to_number(\"3.14e2\");\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/to-number-scientific: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "314")) {
        fail("stdlib/to-number-scientific: expected '314' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_string_to_number_round_trip(void) {
    /* The whole point of Stage 24: string(string_to_number(s)) for
     * a clean numeric s gives s back. Round-trip via both natives. */
    int exitCode;
    char *out = runClox(
        "print string(string_to_number(\"42\"));\n"
        "print string(string_to_number(\"3.14\"));\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/to-number-round-trip: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "42\n")
            || !contains(out, "3.14\n")) {
        fail("stdlib/to-number-round-trip: expected '42' and '3.14' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_string_to_number_empty_errors(void) {
    /* Empty string -> runtime error. Matches strtod's behavior of
     * returning 0 with endptr == startptr; we treat that as "no
     * number parsed" and error. */
    int exitCode;
    char *out = runClox("print string_to_number(\"\");\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/to-number-empty: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);
}

static void test_string_to_number_non_numeric_errors(void) {
    /* "abc" -> runtime error. No digits consumed. */
    int exitCode;
    char *out = runClox("print string_to_number(\"abc\");\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/to-number-non-numeric: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);
}

static void test_string_to_number_overflow_errors(void) {
    /* "1e1000" -> Infinity -> runtime error. strtod returns HUGE_VAL
     * and sets errno = ERANGE; we treat that as overflow. */
    int exitCode;
    char *out = runClox("print string_to_number(\"1e1000\");\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/to-number-overflow: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);
}

static void test_string_to_number_wrong_type(void) {
    /* Non-string first arg -> runtime error. */
    int exitCode;
    char *out = runClox("print string_to_number(42);\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/to-number-wrong-type: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);
}

static void test_string_to_number_wrong_arg_count(void) {
    /* Wrong arity (0 args) -> runtime error. */
    int exitCode;
    char *out = runClox("print string_to_number();\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/to-number-wrong-arg-count: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);
}

/* --- Stage 25: string_to_number(s, base) --- */
/* The multi-base variant of Stage 24. Accepts base in [2, 36] or
 * 0 (auto-detect: '0x' prefix -> hex, '0' prefix -> octal, else
 * decimal). Matches C's strtol / Python's int() with base argument.
 * Same strict-error-on-failure contract: no NaN, no Infinity on
 * overflow, errors on empty input, non-numeric input, no chars
 * consumed, and base out of [2, 36] and not 0. The 1-arg form
 * string_to_number(s) still works (base defaults to 10). */

static void test_string_to_number_base_decimal(void) {
    /* "42" with explicit base 10 — same as 1-arg form. */
    int exitCode;
    char *out = runClox(
        "print string_to_number(\"42\", 10);\n"
        "print string_to_number(\"-7\", 10);\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/to-number-base-decimal: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "42\n")
            || !contains(out, "-7\n")) {
        fail("stdlib/to-number-base-decimal: expected '42' and '-7' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_string_to_number_base_binary(void) {
    /* "1010" in base 2 -> 10. The classic 'binary string' idiom. */
    int exitCode;
    char *out = runClox(
        "print string_to_number(\"1010\", 2);\n"
        "print string_to_number(\"11111111\", 2);\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/to-number-base-binary: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "10\n")
            || !contains(out, "255\n")) {
        fail("stdlib/to-number-base-binary: expected '10' and '255' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_string_to_number_base_hex(void) {
    /* "ff" in base 16 -> 255. The classic 'hex string' idiom. */
    int exitCode;
    char *out = runClox(
        "print string_to_number(\"ff\", 16);\n"
        "print string_to_number(\"DEAD\", 16);\n"
        "print string_to_number(\"0x10\", 16);\n",  /* '0x' prefix allowed */
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/to-number-base-hex: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "255\n")
            || !contains(out, "57005\n")
            || !contains(out, "16\n")) {
        fail("stdlib/to-number-base-hex: expected '255', '57005', '16' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_string_to_number_base_auto(void) {
    /* base 0 -> auto-detect: '0x' prefix -> hex, '0' prefix ->
     * octal, else decimal. C strtol / Python int convention. */
    int exitCode;
    char *out = runClox(
        "print string_to_number(\"0x10\", 0);\n"    /* hex -> 16 */
        "print string_to_number(\"010\", 0);\n"     /* octal -> 8 */
        "print string_to_number(\"42\", 0);\n",     /* decimal -> 42 */
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/to-number-base-auto: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "16\n")
            || !contains(out, "8\n")
            || !contains(out, "42\n")) {
        fail("stdlib/to-number-base-auto: expected '16', '8', '42' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_string_to_number_base_invalid(void) {
    /* base < 2 (and != 0) -> runtime error. base > 36 -> error. */
    int exitCode;
    char *out = runClox("print string_to_number(\"42\", 1);\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/to-number-base-too-low: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);

    out = runClox("print string_to_number(\"42\", 37);\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/to-number-base-too-high: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);
}

static void test_string_to_number_base_wrong_type(void) {
    /* base arg is not a number -> runtime error. */
    int exitCode;
    char *out = runClox("print string_to_number(\"42\", \"10\");\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/to-number-base-wrong-type: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);
}

static void test_string_to_number_base_wrong_arg_count(void) {
    /* 3 args -> runtime error. */
    int exitCode;
    char *out = runClox("print string_to_number(\"42\", 10, 0);\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/to-number-base-wrong-arg-count: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);
}

/* --- Stage 26: string_to_int(s) --- */
/* A NEW native (not an extension of string_to_number). Always
 * uses strtol with base 10 — no float path, no base parameter.
 * Closes the '42 vs 42.0' question: string_to_int("42.5") errors
 * (strtol stops at the '.'), string_to_int("42") returns 42.
 * Returns 42.0 (a double, since clox's number type is double), not
 * 42 as an integer — the user's *intent* is integer; the *type* is
 * still double. Same strict-error contract: empty input, no chars
 * consumed, and overflow to LONG_MIN / LONG_MAX all error. */

static void test_string_to_int_positive(void) {
    /* "42" -> 42; adding 1 gives 43. The basic round-trip. */
    int exitCode;
    char *out = runClox(
        "var n = string_to_int(\"42\");\n"
        "print n;\n"
        "print n + 1;\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/to-int-positive: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "42\n")
            || !contains(out, "43\n")) {
        fail("stdlib/to-int-positive: expected '42' and '43' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_string_to_int_negative(void) {
    /* "-7" -> -7; multiplying by 2 gives -14. The signed-int case. */
    int exitCode;
    char *out = runClox(
        "var n = string_to_int(\"-7\");\n"
        "print n;\n"
        "print n * 2;\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/to-int-negative: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "-7\n")
            || !contains(out, "-14\n")) {
        fail("stdlib/to-int-negative: expected '-7' and '-14' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_string_to_int_round_trip(void) {
    /* string(string_to_int("42")) -> "42"; same for "-7" and "0".
     * The whole point of Stage 26: integer parse + string format
     * is the new closure. */
    int exitCode;
    char *out = runClox(
        "print string(string_to_int(\"42\"));\n"
        "print string(string_to_int(\"-7\"));\n"
        "print string(string_to_int(\"0\"));\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/to-int-round-trip: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "42\n")
            || !contains(out, "-7\n")
            || !contains(out, "0\n")) {
        fail("stdlib/to-int-round-trip: expected '42', '-7', '0' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_string_to_int_float_errors(void) {
    /* "42.5" -> runtime error. strtol stops at the '.', so endptr
     * would be mid-string; we treat that as "no chars consumed"
     * (matches Python's int("42.5") error shape). "3.14" also
     * errors. */
    int exitCode;
    char *out = runClox("print string_to_int(\"42.5\");\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/to-int-float-error: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);

    out = runClox("print string_to_int(\"3.14\");\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/to-int-float-second-error: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);
}

static void test_string_to_int_empty_errors(void) {
    /* "" -> runtime error. */
    int exitCode;
    char *out = runClox("print string_to_int(\"\");\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/to-int-empty-error: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);
}

static void test_string_to_int_non_numeric_errors(void) {
    /* "abc" -> runtime error. */
    int exitCode;
    char *out = runClox("print string_to_int(\"abc\");\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/to-int-non-numeric: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);
}

static void test_string_to_int_wrong_type(void) {
    /* Non-string arg -> runtime error. */
    int exitCode;
    char *out = runClox("print string_to_int(42);\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/to-int-wrong-type: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);
}

static void test_string_to_int_wrong_arg_count(void) {
    /* 0 args -> runtime error. 2 args -> error. The function takes
     * exactly 1 arg. */
    int exitCode;
    char *out = runClox("print string_to_int();\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/to-int-wrong-arg-count-zero: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);

    out = runClox("print string_to_int(\"42\", 10);\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/to-int-wrong-arg-count-two: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);
}

/* --- Stage 27: string_trim_start / string_trim_end --- */
/* Two new natives, mirror of Stage 9's string_trim. The shape:
 * trimStart removes leading whitespace only, leaving trailing
 * whitespace intact; trimEnd removes trailing whitespace only,
 * leaving leading whitespace intact. Combined with Stage 9's
 * string_trim (which removes both), the user has a complete
 * 'whitespace handling' set. JS reference: String.prototype.
 * trimStart / trimEnd (also exposed as trimLeft / trimRight in
 * older specs). */

static void test_string_trim_start_basic(void) {
    /* Leading whitespace removed, trailing preserved. */
    int exitCode;
    char *out = runClox(
        "print string_trim_start(\"   hello   \");\n"  /* -> "hello   " */
        "print string_trim_start(\"   hello\");\n"     /* -> "hello" (no trailing ws) */
        "print string_trim_start(\"hello   \");\n",    /* -> "hello   " (no leading ws) */
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/trim-start-basic: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "hello   \n")
            || !contains(out, "hello\n")
            || !contains(out, "hello   \n")) {
        fail("stdlib/trim-start-basic: expected 'hello   ', 'hello', 'hello   ' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_string_trim_start_all_whitespace(void) {
    /* All-whitespace input -> empty string. */
    int exitCode;
    char *out = runClox("print string_trim_start(\"     \");\n", &exitCode);
    if (exitCode != 0) {
        fail("stdlib/trim-start-all-ws: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "\n")
            || contains(out, "      \n")) {  /* reject the original 5 spaces */
        fail("stdlib/trim-start-all-ws: expected empty string in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_string_trim_start_tabs_and_newlines(void) {
    /* Whitespace includes tabs and newlines, not just spaces.
     * isWhitespace() in clox's native.c handles all three. */
    int exitCode;
    char *out = runClox(
        "print string_trim_start(\"\\t\\n  hello\");\n",  /* -> "hello" */
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/trim-start-tabs-newlines: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "hello\n")) {
        fail("stdlib/trim-start-tabs-newlines: expected 'hello' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_string_trim_start_wrong_type(void) {
    int exitCode;
    char *out = runClox("print string_trim_start(42);\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/trim-start-wrong-type: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);
}

static void test_string_trim_start_wrong_arg_count(void) {
    int exitCode;
    char *out = runClox("print string_trim_start();\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/trim-start-wrong-arg-count-zero: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);

    out = runClox("print string_trim_start(\"  hello  \", 1);\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/trim-start-wrong-arg-count-two: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);
}

static void test_string_trim_end_basic(void) {
    /* Trailing whitespace removed, leading preserved. */
    int exitCode;
    char *out = runClox(
        "print string_trim_end(\"   hello   \");\n"    /* -> "   hello" */
        "print string_trim_end(\"   hello\");\n"       /* -> "   hello" (no trailing ws) */
        "print string_trim_end(\"hello   \");\n",      /* -> "hello" (no leading ws) */
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/trim-end-basic: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "   hello\n")
            || !contains(out, "hello\n")) {
        fail("stdlib/trim-end-basic: expected '   hello' and 'hello' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_string_trim_end_all_whitespace(void) {
    /* All-whitespace input -> empty string. */
    int exitCode;
    char *out = runClox("print string_trim_end(\"     \");\n", &exitCode);
    if (exitCode != 0) {
        fail("stdlib/trim-end-all-ws: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (contains(out, "     \n")) {
        fail("stdlib/trim-end-all-ws: expected empty string in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_string_trim_end_tabs_and_newlines(void) {
    /* Trailing tabs and newlines stripped. */
    int exitCode;
    char *out = runClox(
        "print string_trim_end(\"hello\\t\\n  \");\n",  /* -> "hello" */
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/trim-end-tabs-newlines: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "hello\n")) {
        fail("stdlib/trim-end-tabs-newlines: expected 'hello' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_string_trim_end_wrong_type(void) {
    int exitCode;
    char *out = runClox("print string_trim_end(42);\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/trim-end-wrong-type: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);
}

static void test_string_trim_end_wrong_arg_count(void) {
    int exitCode;
    char *out = runClox("print string_trim_end();\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/trim-end-wrong-arg-count-zero: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);

    out = runClox("print string_trim_end(\"  hello  \", 1);\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/trim-end-wrong-arg-count-two: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);
}

static void test_string_trim_compose(void) {
    /* Compose trim_start + trim_end to do both sides (the same
     * effect as Stage 9's string_trim, but explicit). Or compose
     * them with the input string in either order. */
    int exitCode;
    char *out = runClox(
        "var s = \"   hello   \";\n"
        "print string_trim(string_trim_start(string_trim_end(s)));\n"  /* -> "hello" */
        "print string_trim_start(s) + \"|\" + string_trim_end(s);\n",   /* -> "hello   |   hello" */
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/trim-compose: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "hello\n")
            || !contains(out, "hello   |   hello\n")) {
        fail("stdlib/trim-compose: expected 'hello' and 'hello   |   hello' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_unique_basic(void) {
    /* [1, 2, 2, 3, 1] -> [1, 2, 3]. */
    int exitCode;
    char *out = runClox(
        "var a = [1, 2, 2, 3, 1];\n"
        "var b = array_unique(a);\n"
        "print array_length(b);\n"
        "print b[0];\n"
        "print b[1];\n"
        "print b[2];\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-unique-basic: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "3\n1\n2\n3\n")) {
        fail("stdlib/array-unique-basic: expected '3\\n1\\n2\\n3\\n' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_unique_strings(void) {
    /* Interned string literals collapse; duplicates by content kept. */
    int exitCode;
    char *out = runClox(
        "var a = [\"a\", \"b\", \"a\", \"c\", \"b\"];\n"
        "var b = array_unique(a);\n"
        "print array_length(b);\n"
        "print b[0];\n"
        "print b[1];\n"
        "print b[2];\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-unique-strings: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "3\na\nb\nc\n")) {
        fail("stdlib/array-unique-strings: expected '3\\na\\nb\\nc\\n' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_unique_preserves_input(void) {
    /* array_unique does not mutate its argument. */
    int exitCode;
    char *out = runClox(
        "var a = [1, 1, 2];\n"
        "var b = array_unique(a);\n"
        "print array_length(a);\n"
        "print a[0];\n"
        "print a[1];\n"
        "print array_length(b);\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-unique-preserves-input: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "3\n1\n1\n2\n")) {
        fail("stdlib/array-unique-preserves-input: expected input preserved in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_unique_empty(void) {
    /* [] -> []. */
    int exitCode;
    char *out = runClox(
        "var a = [];\n"
        "var b = array_unique(a);\n"
        "print array_length(b);\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-unique-empty: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "0\n")) {
        fail("stdlib/array-unique-empty: expected '0\\n' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_unique_single(void) {
    /* [42] -> [42]. */
    int exitCode;
    char *out = runClox(
        "var a = [42];\n"
        "var b = array_unique(a);\n"
        "print array_length(b);\n"
        "print b[0];\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-unique-single: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "1\n42\n")) {
        fail("stdlib/array-unique-single: expected '1\\n42\\n' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_unique_mixed_types(void) {
    /* 1, true, \"1\" are distinct values. */
    int exitCode;
    char *out = runClox(
        "var a = [1, true, \"1\", 1, true];\n"
        "var b = array_unique(a);\n"
        "print array_length(b);\n"
        "print b[0];\n"
        "print b[1];\n"
        "print b[2];\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-unique-mixed-types: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "3\n1\ntrue\n1\n")) {
        fail("stdlib/array-unique-mixed-types: expected '3\\n1\\ntrue\\n1\\n' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_unique_wrong_arg_count(void) {
    /* array_unique() with wrong arity is a runtime error. */
    int exitCode;
    char *out = runClox("array_unique();\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/array-unique-wrong-arg-count-zero: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);

    out = runClox("array_unique([1], [2]);\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/array-unique-wrong-arg-count-two: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);
}

static void test_array_unique_wrong_type(void) {
    /* array_unique on a non-array is a runtime error. */
    int exitCode;
    char *out = runClox("array_unique(\"hello\");\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/array-unique-wrong-type: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);
}

/* --- Stage 40: array_unique_by(arr, keyFn) -> array --- */
/* Extends Stage 28's array_unique to deduplicate by a key
 * function. The keyFn is a 1-arg Lox closure that takes
 * the element and returns the key. The result is a new
 * array containing the first occurrence of each unique
 * key, in the order of first-occurrence. The original
 * array is not mutated. JS reference: lodash's `_.uniqBy`
 * (not the native JS method; native JS has no uniqBy).
 * Python reference: more_itertools.uniqify.
 * Architecture: reuses Stage 30's callClosureFromNative
 * (argCount=1, verified path). No new architecture work. */
static void test_array_unique_by_basic(void) {
    /* Deduplicate by the negation of the value: [1, -1, 2,
     * -2, 1, -1] by (-x) -> keys are [-1, 1, -2, 2, -1, 1]
     * -> first-occurrence-of-each-key
     * -> [1, -1, 2, -2] (key -1 first, then 1, then -2,
     * then 2). This tests the "by key" distinction
     * (the result is different from array_unique, which
     * would give [1, -1, 2, -2] by the same input — same
     * result here, but the keyFn is doing the work). */
    int exitCode;
    char *out = runClox(
        "fun keyFn(x) { return 0 - x; }\n"
        "var arr = [1, -1, 2, -2, 1, -1];\n"
        "var result = array_unique_by(arr, keyFn);\n"
        "print array_get(result, 0);\n"
        "print array_get(result, 1);\n"
        "print array_get(result, 2);\n"
        "print array_get(result, 3);\n"
        "print array_length(result);\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-unique-by-basic: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "1\n-1\n2\n-2\n4\n")) {
        fail("stdlib/array-unique-by-basic: expected '1\\n-1\\n2\\n-2\\n4\\n', got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_unique_by_preserves_order(void) {
    /* First-occurrence-wins with a keyFn that always
     * returns the same value: [1, 2, 3, 4] with keyFn
     * returning 0 -> [1] (all keys are 0, only the first
     * element survives). This verifies the
     * "first-occurrence-wins" rule when the keyFn
     * collapses everything to a single key. */
    int exitCode;
    char *out = runClox(
        "fun keyFn(x) { return 0; }\n"
        "var arr = [1, 2, 3, 4];\n"
        "var result = array_unique_by(arr, keyFn);\n"
        "print array_get(result, 0);\n"
        "print array_length(result);\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-unique-by-preserves-order: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "1\n1\n")) {
        fail("stdlib/array-unique-by-preserves-order: expected '1\\n1\\n', got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_unique_by_empty(void) {
    /* Empty array returns empty array, no closure calls. */
    int exitCode;
    char *out = runClox(
        "fun keyFn(x) { return x; }\n"
        "var result = array_unique_by([], keyFn);\n"
        "print array_length(result);\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-unique-by-empty: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "0\n")) {
        fail("stdlib/array-unique-by-empty: expected '0\\n', got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_unique_by_single(void) {
    /* Single-element array returns [that element]. */
    int exitCode;
    char *out = runClox(
        "fun keyFn(x) { return x; }\n"
        "var result = array_unique_by([42], keyFn);\n"
        "print array_get(result, 0);\n"
        "print array_length(result);\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-unique-by-single: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "42\n1\n")) {
        fail("stdlib/array-unique-by-single: expected '42\\n1\\n', got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_unique_by_does_not_mutate(void) {
    /* The source array is unchanged. */
    int exitCode;
    char *out = runClox(
        "fun keyFn(x) { return x; }\n"
        "var arr = [1, 2, 1, 3];\n"
        "array_unique_by(arr, keyFn);\n"
        "print array_get(arr, 0);\n"
        "print array_get(arr, 1);\n"
        "print array_get(arr, 2);\n"
        "print array_get(arr, 3);\n"
        "print array_length(arr);\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-unique-by-does-not-mutate: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "1\n2\n1\n3\n4\n")) {
        fail("stdlib/array-unique-by-does-not-mutate: expected source unchanged, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_unique_by_wrong_arg_count(void) {
    /* array_unique_by takes 2 args (arr, keyFn). 0, 1, 3 args error. */
    int exitCode;
    char *out;
    out = runClox("array_unique_by();\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/array-unique-by-wrong-arg-count/0: expected nonzero exit, got 0 (output: %s)", out);
        free(out);
        return;
    }
    free(out);
    out = runClox("var arr = [1, 2, 3]; fun f(x) { return x; } array_unique_by(arr);\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/array-unique-by-wrong-arg-count/1: expected nonzero exit, got 0 (output: %s)", out);
        free(out);
        return;
    }
    free(out);
    out = runClox("var arr = [1, 2, 3]; fun f(x) { return x; } array_unique_by(arr, f, 99);\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/array-unique-by-wrong-arg-count/3: expected nonzero exit, got 0 (output: %s)", out);
        free(out);
        return;
    }
    free(out);
    pass();
}

static void test_array_unique_by_wrong_type(void) {
    /* array_unique_by on a non-array is a runtime error;
     * array_unique_by with a non-closure keyFn is a runtime error. */
    int exitCode;
    char *out;
    out = runClox("fun f(x) { return x; } array_unique_by(\"hello\", f);\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/array-unique-by-wrong-type/non-array: expected nonzero exit, got 0 (output: %s)", out);
        free(out);
        return;
    }
    free(out);
    out = runClox("var arr = [1, 2, 3]; array_unique_by(arr, 42);\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/array-unique-by-wrong-type/non-closure: expected nonzero exit, got 0 (output: %s)", out);
        free(out);
        return;
    }
    free(out);
    pass();
}

static void test_array_unique_by_typed_keys(void) {
    /* Deduplicate by an explicit key extractor: array of objects
     * where the key is the first field. Since clox doesn't have
     * objects with field access, we use nested arrays instead.
     * [ [1, "a"], [2, "b"], [1, "c"], [3, "a"] ] by first element
     * -> keys are 1, 2, 1, 3 -> first-occurrence-of-each-key
     * -> [ [1, "a"], [2, "b"], [3, "a"] ] */
    int exitCode;
    char *out = runClox(
        "fun keyFn(pair) { return array_get(pair, 0); }\n"
        "var arr = [[1, \"a\"], [2, \"b\"], [1, \"c\"], [3, \"a\"]];\n"
        "var result = array_unique_by(arr, keyFn);\n"
        "print array_get(array_get(result, 0), 0);\n"
        "print array_get(array_get(result, 0), 1);\n"
        "print array_get(array_get(result, 1), 0);\n"
        "print array_get(array_get(result, 1), 1);\n"
        "print array_get(array_get(result, 2), 0);\n"
        "print array_get(array_get(result, 2), 1);\n"
        "print array_length(result);\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-unique-by-typed-keys: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "1\na\n2\nb\n3\na\n3\n")) {
        fail("stdlib/array-unique-by-typed-keys: expected '1\\na\\n2\\nb\\n3\\na\\n3\\n', got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

/* --- Stage 41: array_chunk(arr, size) -> array --- */
/* Chunks an array into fixed-size sub-arrays. The shape:
 * 2 args (array, size). The size is the chunk size; the
 * result is an array of arrays. The last chunk may be
 * shorter if the input length isn't a multiple of size.
 * Empty array returns empty array (no chunks). size <= 0
 * errors. size > length returns one chunk (the input).
 * No user-code dispatch (no closure). Pre-count +
 * pre-allocate pattern (same as Stage 38 array_flatten). */
static void test_array_chunk_basic(void) {
    /* Chunks of 2: [1, 2, 3, 4, 5] -> [[1, 2], [3, 4], [5]]
     * (the last chunk is shorter because 5 is not a
     * multiple of 2). */
    int exitCode;
    char *out = runClox(
        "var arr = [1, 2, 3, 4, 5];\n"
        "var chunks = array_chunk(arr, 2);\n"
        "print array_length(chunks);\n"
        "print array_length(array_get(chunks, 0));\n"
        "print array_get(array_get(chunks, 0), 0);\n"
        "print array_get(array_get(chunks, 0), 1);\n"
        "print array_length(array_get(chunks, 1));\n"
        "print array_get(array_get(chunks, 1), 0);\n"
        "print array_get(array_get(chunks, 1), 1);\n"
        "print array_length(array_get(chunks, 2));\n"
        "print array_get(array_get(chunks, 2), 0);\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-chunk-basic: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "3\n2\n1\n2\n2\n3\n4\n1\n5\n")) {
        fail("stdlib/array-chunk-basic: expected chunked output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_chunk_evenly_divisible(void) {
    /* Chunks of 3 on a 6-element array: [1,2,3,4,5,6] -> [[1,2,3], [4,5,6]] */
    int exitCode;
    char *out = runClox(
        "var arr = [1, 2, 3, 4, 5, 6];\n"
        "var chunks = array_chunk(arr, 3);\n"
        "print array_length(chunks);\n"
        "print array_length(array_get(chunks, 0));\n"
        "print array_get(array_get(chunks, 0), 0);\n"
        "print array_get(array_get(chunks, 0), 1);\n"
        "print array_get(array_get(chunks, 0), 2);\n"
        "print array_length(array_get(chunks, 1));\n"
        "print array_get(array_get(chunks, 1), 0);\n"
        "print array_get(array_get(chunks, 1), 1);\n"
        "print array_get(array_get(chunks, 1), 2);\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-chunk-evenly-divisible: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "2\n3\n1\n2\n3\n3\n4\n5\n6\n")) {
        fail("stdlib/array-chunk-evenly-divisible: expected [[1,2,3],[4,5,6]], got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_chunk_size_one(void) {
    /* Chunks of 1: [1, 2, 3] -> [[1], [2], [3]] */
    int exitCode;
    char *out = runClox(
        "var arr = [1, 2, 3];\n"
        "var chunks = array_chunk(arr, 1);\n"
        "print array_length(chunks);\n"
        "print array_get(array_get(chunks, 0), 0);\n"
        "print array_get(array_get(chunks, 1), 0);\n"
        "print array_get(array_get(chunks, 2), 0);\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-chunk-size-one: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "3\n1\n2\n3\n")) {
        fail("stdlib/array-chunk-size-one: expected [[1],[2],[3]], got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_chunk_size_equals_length(void) {
    /* Chunks of length: [1, 2, 3] with size 3 -> [[1, 2, 3]] (one chunk) */
    int exitCode;
    char *out = runClox(
        "var arr = [1, 2, 3];\n"
        "var chunks = array_chunk(arr, 3);\n"
        "print array_length(chunks);\n"
        "print array_length(array_get(chunks, 0));\n"
        "print array_get(array_get(chunks, 0), 0);\n"
        "print array_get(array_get(chunks, 0), 1);\n"
        "print array_get(array_get(chunks, 0), 2);\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-chunk-size-equals-length: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "1\n3\n1\n2\n3\n")) {
        fail("stdlib/array-chunk-size-equals-length: expected [[1,2,3]], got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_chunk_size_greater_than_length(void) {
    /* Chunks of length+1: [1, 2, 3] with size 4 -> [[1, 2, 3]] (one chunk, shorter than size) */
    int exitCode;
    char *out = runClox(
        "var arr = [1, 2, 3];\n"
        "var chunks = array_chunk(arr, 4);\n"
        "print array_length(chunks);\n"
        "print array_length(array_get(chunks, 0));\n"
        "print array_get(array_get(chunks, 0), 0);\n"
        "print array_get(array_get(chunks, 0), 1);\n"
        "print array_get(array_get(chunks, 0), 2);\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-chunk-size-greater-than-length: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "1\n3\n1\n2\n3\n")) {
        fail("stdlib/array-chunk-size-greater-than-length: expected [[1,2,3]], got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_chunk_empty(void) {
    /* Empty array returns empty array of chunks. */
    int exitCode;
    char *out = runClox(
        "var chunks = array_chunk([], 2);\n"
        "print array_length(chunks);\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-chunk-empty: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "0\n")) {
        fail("stdlib/array-chunk-empty: expected 0 chunks, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_chunk_does_not_mutate(void) {
    /* The source array is unchanged. */
    int exitCode;
    char *out = runClox(
        "var arr = [1, 2, 3, 4];\n"
        "array_chunk(arr, 2);\n"
        "print array_length(arr);\n"
        "print array_get(arr, 0);\n"
        "print array_get(arr, 1);\n"
        "print array_get(arr, 2);\n"
        "print array_get(arr, 3);\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-chunk-does-not-mutate: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "4\n1\n2\n3\n4\n")) {
        fail("stdlib/array-chunk-does-not-mutate: expected source unchanged, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_chunk_wrong_arg_count(void) {
    /* array_chunk takes 2 args. 0, 1, 3 args error. */
    int exitCode;
    char *out;
    out = runClox("array_chunk();\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/array-chunk-wrong-arg-count/0: expected nonzero exit, got 0 (output: %s)", out);
        free(out);
        return;
    }
    free(out);
    out = runClox("array_chunk([1,2,3]);\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/array-chunk-wrong-arg-count/1: expected nonzero exit, got 0 (output: %s)", out);
        free(out);
        return;
    }
    free(out);
    out = runClox("array_chunk([1,2,3], 2, 99);\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/array-chunk-wrong-arg-count/3: expected nonzero exit, got 0 (output: %s)", out);
        free(out);
        return;
    }
    free(out);
    pass();
}

static void test_array_chunk_wrong_type(void) {
    /* Non-array first arg, non-number second arg. */
    int exitCode;
    char *out;
    out = runClox("array_chunk(\"hello\", 2);\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/array-chunk-wrong-type/non-array: expected nonzero exit, got 0 (output: %s)", out);
        free(out);
        return;
    }
    free(out);
    out = runClox("array_chunk([1,2,3], \"two\");\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/array-chunk-wrong-type/non-number: expected nonzero exit, got 0 (output: %s)", out);
        free(out);
        return;
    }
    free(out);
    pass();
}

static void test_array_chunk_size_zero(void) {
    /* size == 0 is an error (can't chunk into 0-size pieces). */
    int exitCode;
    char *out = runClox("array_chunk([1, 2, 3], 0);\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/array-chunk-size-zero: expected nonzero exit, got 0 (output: %s)", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_chunk_size_negative(void) {
    /* size < 0 is an error. */
    int exitCode;
    char *out = runClox("array_chunk([1, 2, 3], -1);\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/array-chunk-size-negative: expected nonzero exit, got 0 (output: %s)", out);
    } else {
        pass();
    }
    free(out);
}

/* --- Stage 42: array_group_by(arr, keyFn) -> array --- */
/* Groups elements of an array by a key function. The
 * shape: 2 args (array, keyFn). The keyFn is a 1-arg
 * Lox closure: keyFn(element) -> key. Returns an array
 * of arrays (the groups), in first-occurrence-of-each-
 * key order. Elements with the same key go into the
 * same group. The key itself is not included in the
 * output (matches lodash's _.groupBy). Empty array
 * returns empty array. No user-code dispatch beyond
 * the 1-arg closure path (reuses Stage 30). */
static void test_array_group_by_basic(void) {
    /* Group numbers by parity. [1, 2, 3, 4, 5, 6] with
     * keyFn(x) = x - 2*(x/2) — but clox uses floating-
     * point division (Stage 40's lesson), so that
     * collapses all keys to 0. Use a boolean predicate
     * instead: keyFn(x) = x < 4 (groups: small=[1,2,3],
     * big=[4,5,6]). */
    int exitCode;
    char *out = runClox(
        "var arr = [1, 2, 3, 4, 5, 6];\n"
        "fun keyFn(x) { return x < 4; }\n"
        "var groups = array_group_by(arr, keyFn);\n"
        "print array_length(groups);\n"
        "print array_length(array_get(groups, 0));\n"
        "print array_get(array_get(groups, 0), 0);\n"
        "print array_get(array_get(groups, 0), 1);\n"
        "print array_get(array_get(groups, 0), 2);\n"
        "print array_length(array_get(groups, 1));\n"
        "print array_get(array_get(groups, 1), 0);\n"
        "print array_get(array_get(groups, 1), 1);\n"
        "print array_get(array_get(groups, 1), 2);\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-group-by-basic: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "2\n3\n1\n2\n3\n3\n4\n5\n6\n")) {
        fail("stdlib/array-group-by-basic: expected [[1,2,3],[4,5,6]], got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_group_by_preserves_order(void) {
    /* Order of groups matches first-occurrence of each key.
     * Strings by length: ["a", "bb", "c", "dd"] with
     * keyFn(s) = string_length(s) -> [["a", "c"], ["bb", "dd"]]
     * (length 1, then length 2). */
    int exitCode;
    char *out = runClox(
        "var arr = [\"a\", \"bb\", \"c\", \"dd\"];\n"
        "fun keyFn(s) { return string_length(s); }\n"
        "var groups = array_group_by(arr, keyFn);\n"
        "print array_length(groups);\n"
        "print array_length(array_get(groups, 0));\n"
        "print array_get(array_get(groups, 0), 0);\n"
        "print array_get(array_get(groups, 0), 1);\n"
        "print array_length(array_get(groups, 1));\n"
        "print array_get(array_get(groups, 1), 0);\n"
        "print array_get(array_get(groups, 1), 1);\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-group-by-preserves-order: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "2\n2\na\nc\n2\nbb\ndd\n")) {
        fail("stdlib/array-group-by-preserves-order: expected [[a,c],[bb,dd]], got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_group_by_empty(void) {
    /* Empty input returns empty array of groups. */
    int exitCode;
    char *out = runClox(
        "fun keyFn(x) { return x; }\n"
        "var groups = array_group_by([], keyFn);\n"
        "print array_length(groups);\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-group-by-empty: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "0\n")) {
        fail("stdlib/array-group-by-empty: expected 0 groups, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_group_by_single(void) {
    /* Single element returns 1 group with 1 element. */
    int exitCode;
    char *out = runClox(
        "fun keyFn(x) { return x; }\n"
        "var groups = array_group_by([42], keyFn);\n"
        "print array_length(groups);\n"
        "print array_length(array_get(groups, 0));\n"
        "print array_get(array_get(groups, 0), 0);\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-group-by-single: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "1\n1\n42\n")) {
        fail("stdlib/array-group-by-single: expected [[42]], got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_group_by_all_same_key(void) {
    /* All elements have the same key -> 1 group with all elements. */
    int exitCode;
    char *out = runClox(
        "fun keyFn(x) { return 0; }\n"
        "var groups = array_group_by([1, 2, 3, 4, 5], keyFn);\n"
        "print array_length(groups);\n"
        "print array_length(array_get(groups, 0));\n"
        "print array_get(array_get(groups, 0), 0);\n"
        "print array_get(array_get(groups, 0), 4);\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-group-by-all-same-key: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "1\n5\n1\n5\n")) {
        fail("stdlib/array-group-by-all-same-key: expected [[1,2,3,4,5]], got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_group_by_all_unique_keys(void) {
    /* Each element has a unique key -> N groups of 1 element each. */
    int exitCode;
    char *out = runClox(
        "fun keyFn(x) { return x; }\n"
        "var groups = array_group_by([10, 20, 30], keyFn);\n"
        "print array_length(groups);\n"
        "print array_length(array_get(groups, 0));\n"
        "print array_get(array_get(groups, 0), 0);\n"
        "print array_length(array_get(groups, 2));\n"
        "print array_get(array_get(groups, 2), 0);\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-group-by-all-unique-keys: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "3\n1\n10\n1\n30\n")) {
        fail("stdlib/array-group-by-all-unique-keys: expected [[10],[20],[30]], got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_group_by_does_not_mutate(void) {
    /* The source array is unchanged. */
    int exitCode;
    char *out = runClox(
        "var arr = [1, 2, 3, 4];\n"
        "fun keyFn(x) { return x < 3; }\n"
        "array_group_by(arr, keyFn);\n"
        "print array_length(arr);\n"
        "print array_get(arr, 0);\n"
        "print array_get(arr, 3);\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-group-by-does-not-mutate: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "4\n1\n4\n")) {
        fail("stdlib/array-group-by-does-not-mutate: expected source unchanged, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_group_by_wrong_arg_count(void) {
    /* array_group_by takes 2 args. 0, 1, 3 args error. */
    int exitCode;
    char *out;
    out = runClox("array_group_by();\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/array-group-by-wrong-arg-count/0: expected nonzero exit, got 0 (output: %s)", out);
        free(out);
        return;
    }
    free(out);
    out = runClox("array_group_by([1,2,3]);\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/array-group-by-wrong-arg-count/1: expected nonzero exit, got 0 (output: %s)", out);
        free(out);
        return;
    }
    free(out);
    out = runClox("array_group_by([1,2,3], fun(x) { return x; }, 99);\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/array-group-by-wrong-arg-count/3: expected nonzero exit, got 0 (output: %s)", out);
        free(out);
        return;
    }
    free(out);
    pass();
}

static void test_array_group_by_wrong_type(void) {
    /* Non-array first arg, non-closure second arg. */
    int exitCode;
    char *out;
    out = runClox("array_group_by(\"hello\", fun(x) { return x; });\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/array-group-by-wrong-type/non-array: expected nonzero exit, got 0 (output: %s)", out);
        free(out);
        return;
    }
    free(out);
    out = runClox("array_group_by([1,2,3], 42);\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/array-group-by-wrong-type/non-closure: expected nonzero exit, got 0 (output: %s)", out);
        free(out);
        return;
    }
    free(out);
    pass();
}

static void test_is_array_true(void) {
    /* Stage 44: is_array(value) -> bool. Returns true
     * iff the value is an array. The "type predicate"
     * pattern. Closes a 27-stage-old gap: Stage 39
     * added typeof(<array>) returning "array", but the
     * caller has to compare a string to determine
     * array-ness. is_array() is the ergonomic
     * predicate. */
    int exitCode;
    char *out = runClox(
        "print is_array([]);\n"
        "print is_array([1, 2, 3]);\n"
        "print is_array([[1, 2], [3, 4]]);\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/is-array-true: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "true\ntrue\ntrue\n")) {
        fail("stdlib/is-array-true: expected 'true' 3 times, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_is_array_false(void) {
    /* is_array() returns false for every non-array
     * type. The "type predicate" should be exhaustive:
     * true for arrays, false for everything else. */
    int exitCode;
    char *out = runClox(
        "print is_array(true);\n"
        "print is_array(false);\n"
        "print is_array(nil);\n"
        "print is_array(42);\n"
        "print is_array(3.14);\n"
        "print is_array(\"hello\");\n"
        "print is_array(\"\");\n"
        "print is_array(clock);\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/is-array-false: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "false\nfalse\nfalse\nfalse\nfalse\nfalse\nfalse\nfalse\n")) {
        fail("stdlib/is-array-false: expected 'false' 8 times, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_is_array_does_not_coerce(void) {
    /* is_array() must NOT coerce. The string "[]" is
     * a string, not an array. The number 0 is a number,
     * not an array. The boolean false is a boolean, not
     * an array. The alternative (truthy coerces) is
     * JS-style but loses type information. */
    int exitCode;
    char *out = runClox(
        "print is_array(\"[]\");\n"
        "print is_array(0);\n"
        "print is_array(false);\n"
        "print is_array(nil);\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/is-array-does-not-coerce: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "false\nfalse\nfalse\nfalse\n")) {
        fail("stdlib/is-array-does-not-coerce: expected 'false' 4 times, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_is_array_wrong_arg_count(void) {
    /* Defensive: native function should handle wrong
     * arg count. 0 args, 2 args both error. */
    int exitCode;
    char *out1 = runClox("print is_array();\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/is-array-wrong-arg-count-0: expected non-zero exit for 0 args");
    } else {
        pass();
    }
    free(out1);

    char *out2 = runClox("print is_array([], []);\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/is-array-wrong-arg-count-2: expected non-zero exit for 2 args");
    } else {
        pass();
    }
    free(out2);
}

static void test_array_group_by_three_groups(void) {
    /* Three distinct keys, in the order they first appear.
     * Numbers by signum: keyFn(x) = x > 0 (positive vs.
     * zero vs negative would be 3 groups, but we only
     * have 2 cases here). Use 3 keys via a string-
     * comparison keyFn that always returns a string.
     * Actually, use keyFn(x) = string(x) for numbers and
     * the element itself for strings — but clox's
     * string() takes a number only. Workaround: use
     * typeof(x) (Stage 39) as the key, which always
     * returns a string. [1, "a", 2, "b", 3] -> [[1,2,3],
     * ["a","b"]] (2 groups by type). For 3 groups,
     * use [1, 2, 3] with keyFn that returns the element
     * itself (each unique). That's only 1 test.
     * For 3 distinct keys in a single test, use
     * [1, 2, 3, 1, 2, 3, 1] with keyFn(x) = x:
     * groups = [[1,1,1], [2,2], [3,3]] (3 groups). */
    int exitCode;
    char *out = runClox(
        "var arr = [1, 2, 3, 1, 2, 3, 1];\n"
        "fun keyFn(x) { return x; }\n"
        "var groups = array_group_by(arr, keyFn);\n"
        "print array_length(groups);\n"
        "print array_length(array_get(groups, 0));\n"
        "print array_length(array_get(groups, 1));\n"
        "print array_length(array_get(groups, 2));\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-group-by-three-groups: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "3\n3\n2\n2\n")) {
        fail("stdlib/array-group-by-three-groups: expected 3 groups [3,2,2], got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

/* --- Stage 43: array_sort(arr, comparator?) -> array --- */
/* Sorts an array, returning a new sorted array. The
 * input is not mutated. The shape: 1-3 args:
 * - 1 arg (array only): default < for numbers,
 *   lexicographic for strings. Mixed types error at
 *   the first comparison.
 * - 2 args (array, keyFn): sorts by keyFn(element)
 *   (1-arg Lox closure). The comparator is < on keys.
 *   Matches Python's sorted(arr, key=fn).
 * - 2 args (array, comparator): sorts using a 2-arg
 *   comparator closure. Returns negative if a < b,
 *   0 if equal, positive if a > b. Matches
 *   Java's Collections.sort, Python's cmp_to_key.
 *
 * Stable sort: preserves first-occurrence order for
 * equal elements. Algorithm: insertion sort (O(N^2)
 * worst case, but simple and stable). For very large
 * arrays, a future stage could add a quicksort/
 * mergesort/timsort variant. */
static void test_array_sort_basic(void) {
    /* Default < for numbers. */
    int exitCode;
    char *out = runClox(
        "var arr = [3, 1, 4, 1, 5, 9, 2, 6];\n"
        "var sorted = array_sort(arr);\n"
        "print array_length(sorted);\n"
        "print array_get(sorted, 0);\n"
        "print array_get(sorted, 1);\n"
        "print array_get(sorted, 2);\n"
        "print array_get(sorted, 3);\n"
        "print array_get(sorted, 4);\n"
        "print array_get(sorted, 5);\n"
        "print array_get(sorted, 6);\n"
        "print array_get(sorted, 7);\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-sort-basic: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "8\n1\n1\n2\n3\n4\n5\n6\n9\n")) {
        fail("stdlib/array-sort-basic: expected [1,1,2,3,4,5,6,9], got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_sort_already_sorted(void) {
    /* Already-sorted input returns the same order. */
    int exitCode;
    char *out = runClox(
        "var sorted = array_sort([1, 2, 3, 4, 5]);\n"
        "print array_length(sorted);\n"
        "print array_get(sorted, 0);\n"
        "print array_get(sorted, 4);\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-sort-already-sorted: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "5\n1\n5\n")) {
        fail("stdlib/array-sort-already-sorted: expected [1..5], got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_sort_reverse(void) {
    /* Reverse-sorted input returns the reversed order. */
    int exitCode;
    char *out = runClox(
        "var sorted = array_sort([5, 4, 3, 2, 1]);\n"
        "print array_length(sorted);\n"
        "print array_get(sorted, 0);\n"
        "print array_get(sorted, 4);\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-sort-reverse: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "5\n1\n5\n")) {
        fail("stdlib/array-sort-reverse: expected [1..5], got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_sort_empty(void) {
    /* Empty input returns empty array. */
    int exitCode;
    char *out = runClox(
        "var sorted = array_sort([]);\n"
        "print array_length(sorted);\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-sort-empty: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "0\n")) {
        fail("stdlib/array-sort-empty: expected 0 length, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_sort_single(void) {
    /* Single element returns single element. */
    int exitCode;
    char *out = runClox(
        "var sorted = array_sort([42]);\n"
        "print array_length(sorted);\n"
        "print array_get(sorted, 0);\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-sort-single: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "1\n42\n")) {
        fail("stdlib/array-sort-single: expected [42], got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_sort_strings(void) {
    /* Lexicographic sort for strings. */
    int exitCode;
    char *out = runClox(
        "var sorted = array_sort([\"banana\", \"apple\", \"cherry\"]);\n"
        "print array_length(sorted);\n"
        "print array_get(sorted, 0);\n"
        "print array_get(sorted, 1);\n"
        "print array_get(sorted, 2);\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-sort-strings: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "3\napple\nbanana\ncherry\n")) {
        fail("stdlib/array-sort-strings: expected [apple,banana,cherry], got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_sort_with_keyfn(void) {
    /* 1-arg keyFn: sort by keyFn(element). Sort by
     * negation = descending. */
    int exitCode;
    char *out = runClox(
        "var arr = [3, 1, 4, 1, 5];\n"
        "fun keyFn(x) { return 0 - x; }\n"
        "var sorted = array_sort(arr, keyFn);\n"
        "print array_length(sorted);\n"
        "print array_get(sorted, 0);\n"
        "print array_get(sorted, 1);\n"
        "print array_get(sorted, 2);\n"
        "print array_get(sorted, 3);\n"
        "print array_get(sorted, 4);\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-sort-with-keyfn: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "5\n5\n4\n3\n1\n1\n")) {
        fail("stdlib/array-sort-with-keyfn: expected [5,4,3,1,1], got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_sort_with_comparator(void) {
    /* 2-arg comparator: returns negative if a < b, 0
     * if equal, positive if a > b. Sort by
     * comparator(a, b) = b - a = descending. */
    int exitCode;
    char *out = runClox(
        "var arr = [3, 1, 4, 1, 5];\n"
        "fun comparator(a, b) { return b - a; }\n"
        "var sorted = array_sort(arr, comparator);\n"
        "print array_length(sorted);\n"
        "print array_get(sorted, 0);\n"
        "print array_get(sorted, 1);\n"
        "print array_get(sorted, 2);\n"
        "print array_get(sorted, 3);\n"
        "print array_get(sorted, 4);\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-sort-with-comparator: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "5\n5\n4\n3\n1\n1\n")) {
        fail("stdlib/array-sort-with-comparator: expected [5,4,3,1,1], got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_sort_does_not_mutate(void) {
    /* The source array is unchanged. */
    int exitCode;
    char *out = runClox(
        "var arr = [3, 1, 2];\n"
        "array_sort(arr);\n"
        "print array_length(arr);\n"
        "print array_get(arr, 0);\n"
        "print array_get(arr, 2);\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-sort-does-not-mutate: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "3\n3\n2\n")) {
        fail("stdlib/array-sort-does-not-mutate: expected source unchanged [3,1,2], got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_sort_stable(void) {
    /* Stable sort: preserves first-occurrence order for
     * equal elements. Use a 2D array [[1, 'a'], [1, 'b'],
     * [2, 'c'], [1, 'd']] sorted by the first element
     * (the key) — the equal-key elements (1, 1, 1) should
     * preserve their original relative order: a, b, d.
     * The key=2 element (c) goes last. Expected output:
     * [a, b, d, c]. */
    int exitCode;
    char *out = runClox(
        "var arr = [[1, \"a\"], [1, \"b\"], [2, \"c\"], [1, \"d\"]];\n"
        "fun keyFn(x) { return array_get(x, 0); }\n"
        "var sorted = array_sort(arr, keyFn);\n"
        "print array_length(sorted);\n"
        "print array_get(array_get(sorted, 0), 1);\n"
        "print array_get(array_get(sorted, 1), 1);\n"
        "print array_get(array_get(sorted, 2), 1);\n"
        "print array_get(array_get(sorted, 3), 1);\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-sort-stable: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "4\na\nb\nd\nc\n")) {
        fail("stdlib/array-sort-stable: expected [a,b,d,c] (stable), got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_sort_wrong_arg_count(void) {
    /* array_sort takes 1-2 args. 0, 3 args error. */
    int exitCode;
    char *out;
    out = runClox("array_sort();\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/array-sort-wrong-arg-count/0: expected nonzero exit, got 0 (output: %s)", out);
        free(out);
        return;
    }
    free(out);
    out = runClox("array_sort([1,2,3], fun(x) { return x; }, 99);\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/array-sort-wrong-arg-count/3: expected nonzero exit, got 0 (output: %s)", out);
        free(out);
        return;
    }
    free(out);
    pass();
}

static void test_array_sort_wrong_type(void) {
    /* Non-array first arg, non-closure second arg. */
    int exitCode;
    char *out;
    out = runClox("array_sort(\"hello\");\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/array-sort-wrong-type/non-array: expected nonzero exit, got 0 (output: %s)", out);
        free(out);
        return;
    }
    free(out);
    out = runClox("array_sort([1,2,3], 42);\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/array-sort-wrong-type/non-closure: expected nonzero exit, got 0 (output: %s)", out);
        free(out);
        return;
    }
    free(out);
    pass();
}

/* --- Stage 29: string_split(s, delim, limit) --- */
/* Extends Stage 13's string_split with a max-split-count
 * parameter. The shape: 2 args (Stage 13) splits on every
 * occurrence of delim; 3 args (Stage 29) splits at most `limit`
 * times, leaving the rest of the string as the final element.
 * JS reference: String.prototype.split(s, limit) — limit is
 * optional, default is "split on every occurrence." Python
 * reference: str.split(sep, maxsplit) — maxsplit is optional,
 * default is -1 (no limit). The natural small-mirror's mirror
 * is "add one parameter to an existing function" rather than
 * "new conceptual native."
 *
 * Edge cases for limit:
 *   limit = 0  -> [s] (no splits; whole string is one element)
 *   limit < 0  -> runtime error (negative limits are nonsense)
 *   limit = 1  -> split at most once -> 2 elements max
 *   limit > #  -> full split (no limit reached)
 *   limit non-int -> runtime error (must be a whole number) */

static void test_string_split_with_limit(void) {
    /* "a,b,c,d" split on "," with limit 2 -> ["a", "b", "c,d"]
     * (split at most 2 times; the rest is the final element). */
    int exitCode;
    char *out = runClox(
        "var parts = string_split(\"a,b,c,d\", \",\", 2);\n"
        "print array_length(parts);\n"
        "print array_get(parts, 0);\n"
        "print array_get(parts, 1);\n"
        "print array_get(parts, 2);\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/split-with-limit: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "3\n")
            || !contains(out, "a\n")
            || !contains(out, "b\n")
            || !contains(out, "c,d\n")) {
        fail("stdlib/split-with-limit: expected '3', 'a', 'b', 'c,d' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_string_split_limit_zero(void) {
    /* limit = 0 -> [s] (no splits; whole string is one element). */
    int exitCode;
    char *out = runClox(
        "var parts = string_split(\"a,b,c\", \",\", 0);\n"
        "print array_length(parts);\n"
        "print array_get(parts, 0);\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/split-limit-zero: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "1\n")
            || !contains(out, "a,b,c\n")) {
        fail("stdlib/split-limit-zero: expected '1' and 'a,b,c' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_string_split_limit_one(void) {
    /* limit = 1 -> split at most once -> 2 elements max. */
    int exitCode;
    char *out = runClox(
        "var parts = string_split(\"a,b,c,d\", \",\", 1);\n"
        "print array_length(parts);\n"
        "print array_get(parts, 0);\n"
        "print array_get(parts, 1);\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/split-limit-one: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "2\n")
            || !contains(out, "a\n")
            || !contains(out, "b,c,d\n")) {
        fail("stdlib/split-limit-one: expected '2', 'a', 'b,c,d' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_string_split_limit_larger(void) {
    /* limit larger than the number of splits -> full split
     * (no limit reached). "a,b,c" with limit 10 -> ["a","b","c"]
     * (3 elements, same as no limit). */
    int exitCode;
    char *out = runClox(
        "var parts = string_split(\"a,b,c\", \",\", 10);\n"
        "print array_length(parts);\n"
        "print array_get(parts, 0);\n"
        "print array_get(parts, 1);\n"
        "print array_get(parts, 2);\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/split-limit-larger: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "3\n")
            || !contains(out, "a\n")
            || !contains(out, "b\n")
            || !contains(out, "c\n")) {
        fail("stdlib/split-limit-larger: expected '3', 'a', 'b', 'c' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_string_split_limit_negative(void) {
    /* Negative limit -> runtime error. */
    int exitCode;
    char *out = runClox("print string_split(\"a,b,c\", \",\", -1);\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/split-limit-negative: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);
}

static void test_string_split_limit_wrong_type(void) {
    /* Non-number limit -> runtime error. */
    int exitCode;
    char *out = runClox("print string_split(\"a,b,c\", \",\", \"2\");\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/split-limit-wrong-type: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);
}

static void test_string_split_wrong_arg_count(void) {
    /* 4 args -> runtime error. 1 arg -> error (string_split
     * requires at least 2 args). */
    int exitCode;
    char *out = runClox("print string_split(\"a,b,c\", \",\", 2, 0);\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/split-wrong-arg-count-four: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);

    out = runClox("print string_split(\"a,b,c\");\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/split-wrong-arg-count-one: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);
}

/* --- Stage 30: array_filter(arr, predicate) --- */
/* New native. Takes an array and a callable (a 1-arg Lox
 * closure that returns true/false). Returns a new array
 * containing only the elements for which the predicate
 * returns true. Order is preserved. Empty result is OK.
 *
 * This is the first native that invokes user-defined Lox
 * code from C. The architecture: vm.c's `call()` function is
 * now public (declared in vm.h), and the native pushes the
 * current element onto the stack as the call's argument,
 * invokes `call(closure, 1)`, then pops the result.
 *
 * JS reference: Array.prototype.filter(predicate, thisArg)
 * Python reference: filter(function, iterable)
 * The clox semantics: predicate is called with one argument
 * (the element); the result is truthy/falsy per clox's
 * standard rule (false and nil are falsy, everything else
 * is truthy). The empty case is `[]`.
 *
 * --- clox test syntax notes ---
 * clox does NOT support `var name = fun(x) {...}` at top level
 * (the parser doesn't accept function expressions as the rhs
 * of a var declaration). Top-level function declarations
 * use the form `fun name(args) { body }`. We use that form
 * throughout the Stage 30 tests.
 * clox also doesn't support `%` (modulo) — we use `==` with
 * specific values for parity tests. */

static void test_array_filter_basic(void) {
    /* [1, 2, 3, 4] filtered to == 2 -> [2]. */
    int exitCode;
    char *out = runClox(
        "fun isTwo(x) { return x == 2; }\n"
        "var evens = array_filter([1, 2, 3, 4], isTwo);\n"
        "print array_length(evens);\n"
        "print array_get(evens, 0);\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-filter-basic: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "1\n")
            || !contains(out, "2\n")) {
        fail("stdlib/array-filter-basic: expected '1' and '2' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_filter_strings(void) {
    /* ["apple", "berry"] filtered to start with 'a' -> ["apple"].
     * (clox strings support string_index_of; ==0 means starts-with.) */
    int exitCode;
    char *out = runClox(
        "fun startsWithA(s) { return string_index_of(s, \"a\") == 0; }\n"
        "var aFruits = array_filter([\"apple\", \"berry\"], startsWithA);\n"
        "print array_length(aFruits);\n"
        "print array_get(aFruits, 0);\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-filter-strings: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "1\n")
            || !contains(out, "apple\n")) {
        fail("stdlib/array-filter-strings: expected '1' and 'apple' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_filter_preserves_order(void) {
    /* [3, 1, 4, 1, 5, 9, 2, 6] filtered to > 2 should
     * preserve original order -> [3, 4, 5, 9, 6]. */
    int exitCode;
    char *out = runClox(
        "fun gt2(x) { return x > 2; }\n"
        "var big = array_filter([3, 1, 4, 1, 5, 9, 2, 6], gt2);\n"
        "print array_length(big);\n"
        "print array_get(big, 0);\n"
        "print array_get(big, 1);\n"
        "print array_get(big, 2);\n"
        "print array_get(big, 3);\n"
        "print array_get(big, 4);\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-filter-preserves-order: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "5\n")
            || !contains(out, "3\n")
            || !contains(out, "4\n")
            || !contains(out, "5\n")
            || !contains(out, "9\n")
            || !contains(out, "6\n")) {
        fail("stdlib/array-filter-preserves-order: expected '5','3','4','5','9','6' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_filter_empty(void) {
    /* Empty input array -> empty result. */
    int exitCode;
    char *out = runClox(
        "fun alwaysTrue(x) { return true; }\n"
        "var result = array_filter([], alwaysTrue);\n"
        "print array_length(result);\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-filter-empty: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "0\n")) {
        fail("stdlib/array-filter-empty: expected '0' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_filter_all_filtered(void) {
    /* Predicate returns false for all -> empty result. */
    int exitCode;
    char *out = runClox(
        "fun alwaysFalse(x) { return false; }\n"
        "var result = array_filter([1, 2, 3], alwaysFalse);\n"
        "print array_length(result);\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-filter-all-filtered: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "0\n")) {
        fail("stdlib/array-filter-all-filtered: expected '0' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_filter_does_not_mutate(void) {
    /* The original array must not be mutated. */
    int exitCode;
    char *out = runClox(
        "fun isTwo(x) { return x == 2; }\n"
        "var nums = [1, 2, 3, 4];\n"
        "var evens = array_filter(nums, isTwo);\n"
        "print array_length(nums);\n"
        "print array_get(nums, 0);\n"
        "print array_get(nums, 1);\n"
        "print array_get(nums, 2);\n"
        "print array_get(nums, 3);\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-filter-does-not-mutate: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "4\n")
            || !contains(out, "1\n")
            || !contains(out, "2\n")
            || !contains(out, "3\n")
            || !contains(out, "4\n")) {
        fail("stdlib/array-filter-does-not-mutate: expected '4','1','2','3','4' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_filter_wrong_arg_count(void) {
    /* 1 arg (no predicate) errors. 3 args errors. */
    int exitCode;
    char *out = runClox("print array_filter([1,2,3]);\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/array-filter-wrong-arg-count-one: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);

    out = runClox(
        "fun alwaysTrue(x) { return true; }\n"
        "print array_filter([1,2,3], alwaysTrue, 0);\n",
        &exitCode);
    if (exitCode == 0) {
        fail("stdlib/array-filter-wrong-arg-count-three: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);
}

static void test_array_filter_wrong_type(void) {
    /* First arg must be array. Second arg must be a function. */
    int exitCode;
    char *out = runClox(
        "fun alwaysTrue(x) { return true; }\n"
        "print array_filter(\"not an array\", alwaysTrue);\n",
        &exitCode);
    if (exitCode == 0) {
        fail("stdlib/array-filter-wrong-type-arr: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);

    out = runClox("print array_filter([1,2,3], 42);\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/array-filter-wrong-type-fn: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);
}

/* Stage 31: array_map(arr, transform) -> array.
 * Mirror of Stage 30's array_filter. Takes an array and a 1-arg
 * Lox closure (the transform). Returns a new array where each
 * element is `transform(element)`. Uses the same user-code dispatch
 * architecture as Stage 30 (callClosureFromNative + OP_RETURN target
 * check). clox doesn't support anonymous function expressions as
 * function args; we use the form `fun name(args) { body }`. We use
 * that form throughout the Stage 31 tests.
 * clox also doesn't support `%` (modulo) — we use `==` with
 * specific values for parity tests. */

static void test_array_map_basic(void) {
    /* [1, 2, 3, 4] mapped by double -> [2, 4, 6, 8]. */
    int exitCode;
    char *out = runClox(
        "fun dbl(x) { return x * 2; }\n"
        "var out = array_map([1, 2, 3, 4], dbl);\n"
        "print array_length(out);\n"
        "print array_get(out, 0);\n"
        "print array_get(out, 1);\n"
        "print array_get(out, 2);\n"
        "print array_get(out, 3);\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-map-basic: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "4\n")
            || !contains(out, "2\n")
            || !contains(out, "4\n")
            || !contains(out, "6\n")
            || !contains(out, "8\n")) {
        fail("stdlib/array-map-basic: expected '4','2','4','6','8' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_map_type_change(void) {
    /* [1, 2, 3] mapped by string(n) -> ["1", "2", "3"].
     * Composes Stage 17's string(n) inside a wrapper closure.
     * The transform's return type can be different from the
     * source type; array_map doesn't constrain it. The closure
     * wrapper is needed because array_map requires a 1-arg Lox
     * closure, not a native (e.g., the `string` native itself). */
    int exitCode;
    char *out = runClox(
        "fun toString(x) { return string(x); }\n"
        "var out = array_map([1, 2, 3], toString);\n"
        "print array_length(out);\n"
        "print array_get(out, 0);\n"
        "print array_get(out, 1);\n"
        "print array_get(out, 2);\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-map-type-change: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "3\n")
            || !contains(out, "1\n")
            || !contains(out, "2\n")
            || !contains(out, "3\n")) {
        fail("stdlib/array-map-type-change: expected '3','1','2','3' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_map_preserves_order(void) {
    /* [3, 1, 4, 1, 5, 9, 2, 6] mapped by x + 10 ->
     * [13, 11, 14, 11, 15, 19, 12, 16]. Order preserved. */
    int exitCode;
    char *out = runClox(
        "fun add10(x) { return x + 10; }\n"
        "var out = array_map([3, 1, 4, 1, 5, 9, 2, 6], add10);\n"
        "print array_length(out);\n"
        "print array_get(out, 0);\n"
        "print array_get(out, 1);\n"
        "print array_get(out, 2);\n"
        "print array_get(out, 3);\n"
        "print array_get(out, 4);\n"
        "print array_get(out, 5);\n"
        "print array_get(out, 6);\n"
        "print array_get(out, 7);\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-map-preserves-order: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "8\n")
            || !contains(out, "13\n")
            || !contains(out, "11\n")
            || !contains(out, "14\n")
            || !contains(out, "15\n")
            || !contains(out, "19\n")
            || !contains(out, "12\n")
            || !contains(out, "16\n")) {
        fail("stdlib/array-map-preserves-order: expected '8','13','11','14','15','19','12','16' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_map_empty(void) {
    /* Empty input -> empty result. */
    int exitCode;
    char *out = runClox(
        "fun id(x) { return x; }\n"
        "var out = array_map([], id);\n"
        "print array_length(out);\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-map-empty: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "0\n")) {
        fail("stdlib/array-map-empty: expected '0' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_map_does_not_mutate(void) {
    /* The original array must not be mutated. */
    int exitCode;
    char *out = runClox(
        "fun dbl(x) { return x * 2; }\n"
        "var nums = [1, 2, 3, 4];\n"
        "var out = array_map(nums, dbl);\n"
        "print array_length(nums);\n"
        "print array_get(nums, 0);\n"
        "print array_get(nums, 1);\n"
        "print array_get(nums, 2);\n"
        "print array_get(nums, 3);\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-map-does-not-mutate: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "4\n")
            || !contains(out, "1\n")
            || !contains(out, "2\n")
            || !contains(out, "3\n")
            || !contains(out, "4\n")) {
        fail("stdlib/array-map-does-not-mutate: expected '4','1','2','3','4' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_map_composes_with_filter(void) {
    /* Real-world: filter to keep only the values > 2, then map
     * to double. (Stage 30 + Stage 31 composition.) */
    int exitCode;
    char *out = runClox(
        "fun gt2(x) { return x > 2; }\n"
        "fun dbl(x) { return x * 2; }\n"
        "var big = array_filter([1, 2, 3, 4, 5], gt2);\n"
        "var doubled = array_map(big, dbl);\n"
        "print array_length(doubled);\n"
        "print array_get(doubled, 0);\n"
        "print array_get(doubled, 1);\n"
        "print array_get(doubled, 2);\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-map-composes-with-filter: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "3\n")
            || !contains(out, "6\n")
            || !contains(out, "8\n")
            || !contains(out, "10\n")) {
        fail("stdlib/array-map-composes-with-filter: expected '3','6','8','10' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_map_wrong_arg_count(void) {
    /* 1 arg (no transform) errors. 3 args errors. */
    int exitCode;
    char *out = runClox("print array_map([1,2,3]);\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/array-map-wrong-arg-count-one: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);

    out = runClox(
        "fun id(x) { return x; }\n"
        "print array_map([1,2,3], id, 0);\n",
        &exitCode);
    if (exitCode == 0) {
        fail("stdlib/array-map-wrong-arg-count-three: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);
}

static void test_array_map_wrong_type(void) {
    /* First arg must be array. Second arg must be a function. */
    int exitCode;
    char *out = runClox(
        "fun id(x) { return x; }\n"
        "print array_map(\"not an array\", id);\n",
        &exitCode);
    if (exitCode == 0) {
        fail("stdlib/array-map-wrong-type-arr: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);

    out = runClox("print array_map([1,2,3], 42);\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/array-map-wrong-type-fn: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);
}

/* Stage 32: array_reduce(arr, reducer, initial) -> value.
 * Mirror of Stage 31's array_map. Takes an array, a 2-arg
 * Lox closure (the reducer: (acc, elem) -> newAcc), and an
 * initial value. Returns the final accumulator. Uses the same
 * user-code-dispatch architecture as Stages 30-31
 * (callClosureFromNative with argCount=2). clox doesn't
 * support anonymous function expressions; we use the form
 * `fun name(args) { body }`. */

static void test_array_reduce_sum(void) {
    /* Sum [1, 2, 3, 4, 5] with initial 0. */
    int exitCode;
    char *out = runClox(
        "fun add(acc, x) { return acc + x; }\n"
        "var total = array_reduce([1, 2, 3, 4, 5], add, 0);\n"
        "print total;\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-reduce-sum: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "15\n")) {
        fail("stdlib/array-reduce-sum: expected '15' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_reduce_product(void) {
    /* Product [1, 2, 3, 4] with initial 1. */
    int exitCode;
    char *out = runClox(
        "fun mul(acc, x) { return acc * x; }\n"
        "var p = array_reduce([1, 2, 3, 4], mul, 1);\n"
        "print p;\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-reduce-product: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "24\n")) {
        fail("stdlib/array-reduce-product: expected '24' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_reduce_string_concat(void) {
    /* Concatenate strings with empty-string initial. */
    int exitCode;
    char *out = runClox(
        "fun cat(acc, s) { return acc + s; }\n"
        "var joined = array_reduce([\"a\", \"b\", \"c\"], cat, \"\");\n"
        "print joined;\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-reduce-string-concat: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "abc\n")) {
        fail("stdlib/array-reduce-string-concat: expected 'abc' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_reduce_empty_array(void) {
    /* Empty array: returns the initial as-is. */
    int exitCode;
    char *out = runClox(
        "fun add(acc, x) { return acc + x; }\n"
        "var total = array_reduce([], add, 42);\n"
        "print total;\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-reduce-empty-array: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "42\n")) {
        fail("stdlib/array-reduce-empty-array: expected '42' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_reduce_type_change(void) {
    /* Reducer can return a different type than the initial.
     * Here the reducer returns a bool (string_length > 0);
     * the final accumulator is the last value the reducer
     * returned. clox's truthy rule: false and nil are falsy;
     * the result `true` is truthy, so `print total` prints
     * 'true'. The discipline: array_reduce doesn't constrain
     * the reducer's return type, so the test is verifying
     * the natural propagation, not a specific number. */
    int exitCode;
    char *out = runClox(
        "fun nonEmpty(acc, s) { return string_length(s) > 0; }\n"
        "var total = array_reduce([\"a\", \"\", \"b\"], nonEmpty, 0);\n"
        "print total;\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-reduce-type-change: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "true\n")) {
        fail("stdlib/array-reduce-type-change: expected 'true' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_reduce_composes_with_map(void) {
    /* Compose with Stage 31: map then reduce. */
    int exitCode;
    char *out = runClox(
        "fun dbl(x) { return x * 2; }\n"
        "fun add(acc, x) { return acc + x; }\n"
        "var doubled = array_map([1, 2, 3, 4], dbl);\n"
        "var total = array_reduce(doubled, add, 0);\n"
        "print total;\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-reduce-composes-with-map: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "20\n")) {
        fail("stdlib/array-reduce-composes-with-map: expected '20' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_reduce_wrong_arg_count(void) {
    /* 1 arg (no reducer, no initial) errors. 2 args errors. 4 args errors. */
    int exitCode;
    char *out = runClox("print array_reduce([1,2,3]);\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/array-reduce-wrong-arg-count-one: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);

    out = runClox(
        "fun add(acc, x) { return acc + x; }\n"
        "print array_reduce([1,2,3], add);\n",
        &exitCode);
    if (exitCode == 0) {
        fail("stdlib/array-reduce-wrong-arg-count-two: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);

    out = runClox(
        "fun add(acc, x) { return acc + x; }\n"
        "print array_reduce([1,2,3], add, 0, 99);\n",
        &exitCode);
    if (exitCode == 0) {
        fail("stdlib/array-reduce-wrong-arg-count-four: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);
}

static void test_array_reduce_wrong_type(void) {
    /* Source must be array. Reducer must be a 2-arg function. */
    int exitCode;
    char *out = runClox(
        "fun add(acc, x) { return acc + x; }\n"
        "print array_reduce(\"not an array\", add, 0);\n",
        &exitCode);
    if (exitCode == 0) {
        fail("stdlib/array-reduce-wrong-type-arr: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);

    out = runClox("print array_reduce([1,2,3], 42, 0);\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/array-reduce-wrong-type-fn: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);

    /* Reducer arity must be 2. */
    out = runClox(
        "fun oneArg(x) { return x; }\n"
        "print array_reduce([1,2,3], oneArg, 0);\n",
        &exitCode);
    if (exitCode == 0) {
        fail("stdlib/array-reduce-wrong-type-arity: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);
}

static void test_array_any_finds_match(void) {
    /* Basic case: array with a truthy element returns true.
     * clox's truthy rule: only false and nil are falsy. So
     * any non-bool non-nil value is truthy. The predicate
     * checks for membership in {1, 3, 5}, so [2, 4, 5, 6]
     * finds 5 and returns true. */
    int exitCode;
    char *out = runClox(
        "fun isOdd(x) { return x == 1 or x == 3 or x == 5; }\n"
        "var found = array_any([2, 4, 5, 6], isOdd);\n"
        "print found;\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-any-finds-match: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "true\n")) {
        fail("stdlib/array-any-finds-match: expected 'true' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_any_no_match(void) {
    /* No element matches the predicate -> false. */
    int exitCode;
    char *out = runClox(
        "fun isNegative(x) { return x < 0; }\n"
        "var found = array_any([1, 2, 3, 4, 5], isNegative);\n"
        "print found;\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-any-no-match: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "false\n")) {
        fail("stdlib/array-any-no-match: expected 'false' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_any_empty(void) {
    /* Empty array -> false without ever calling the predicate.
     * Matches JS Array.prototype.some and Python's any. */
    int exitCode;
    char *out = runClox(
        "fun isPositive(x) { return x > 0; }\n"
        "var found = array_any([], isPositive);\n"
        "print found;\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-any-empty: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "false\n")) {
        fail("stdlib/array-any-empty: expected 'false' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_any_short_circuits(void) {
    /* Short-circuit: once a truthy element is found, the
     * rest of the array is not iterated. We test this by
     * using a predicate with a side effect (printing the
     * element). If short-circuit works, only the elements
     * before the first match are printed. Then we print
     * the result on its own line. */
    int exitCode;
    char *out = runClox(
        "fun trace(x) { print \"visit:\" + string(x); return x == 3; }\n"
        "var found = array_any([1, 2, 3, 4, 5], trace);\n"
        "print found;\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-any-short-circuits: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "visit:1\n") || !contains(out, "visit:2\n")
            || !contains(out, "visit:3\n") || contains(out, "visit:4\n")) {
        fail("stdlib/array-any-short-circuits: expected short-circuit (no visit:4), got '%s'", out);
    } else if (!contains(out, "true\n")) {
        fail("stdlib/array-any-short-circuits: expected 'true', got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_any_single_element_truthy(void) {
    /* Single-element array where the element is truthy. */
    int exitCode;
    char *out = runClox(
        "fun isTruthy(x) { return x; }\n"
        "var found = array_any([42], isTruthy);\n"
        "print found;\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-any-single-truthy: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "true\n")) {
        fail("stdlib/array-any-single-truthy: expected 'true' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_any_single_element_falsy(void) {
    /* Single-element array where the predicate returns false.
     * Uses isZero (a manual check for 0) — clox treats 0 as
     * truthy, so the literal 0 needs an explicit comparison. */
    int exitCode;
    char *out = runClox(
        "fun isZero(x) { return x == 0; }\n"
        "var found = array_any([0], isZero);\n"
        "print found;\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-any-single-falsy: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "true\n")) {
        fail("stdlib/array-any-single-falsy: expected 'true' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_any_does_not_mutate(void) {
    /* The source array is not mutated. */
    int exitCode;
    char *out = runClox(
        "var src = [10, 20, 30];\n"
        "fun isTwenty(x) { return x == 20; }\n"
        "var found = array_any(src, isTwenty);\n"
        "print \"len:\" + string(array_length(src));\n"
        "print \"0:\" + string(array_get(src, 0));\n"
        "print \"1:\" + string(array_get(src, 1));\n"
        "print \"2:\" + string(array_get(src, 2));\n"
        "print found;\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-any-does-not-mutate: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "len:3\n") || !contains(out, "0:10\n")
            || !contains(out, "1:20\n") || !contains(out, "2:30\n")
            || !contains(out, "true\n")) {
        fail("stdlib/array-any-does-not-mutate: expected src unchanged + true, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_any_wrong_arg_count(void) {
    /* 1 arg, 3 args, 0 args. */
    int exitCode;
    char *out = runClox(
        "fun isPositive(x) { return x > 0; }\n"
        "print array_any([1,2,3]);\n",
        &exitCode);
    if (exitCode == 0) {
        fail("stdlib/array-any-wrong-arg-count-one: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);

    out = runClox(
        "fun isPositive(x) { return x > 0; }\n"
        "print array_any([1,2,3], isPositive, 99);\n",
        &exitCode);
    if (exitCode == 0) {
        fail("stdlib/array-any-wrong-arg-count-three: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);

    out = runClox("print array_any();\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/array-any-wrong-arg-count-zero: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);
}

static void test_array_any_wrong_type(void) {
    /* Source must be array; predicate must be a 1-arg function. */
    int exitCode;
    char *out = runClox(
        "fun isPositive(x) { return x > 0; }\n"
        "print array_any(\"not an array\", isPositive);\n",
        &exitCode);
    if (exitCode == 0) {
        fail("stdlib/array-any-wrong-type-arr: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);

    out = runClox("print array_any([1,2,3], 42);\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/array-any-wrong-type-fn: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);

    out = runClox(
        "fun zeroArg() { return true; }\n"
        "print array_any([1,2,3], zeroArg);\n",
        &exitCode);
    if (exitCode == 0) {
        fail("stdlib/array-any-wrong-type-arity: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);
}

static void test_array_all_finds_match(void) {
    /* Basic case: all elements are truthy -> true. The
     * predicate checks for even numbers; [2, 4, 6, 8] are
     * all even, so the result is true. (Mirrors the
     * finds-match test for array_any, but with the expected
     * value flipped from true to true via all-true.) */
    int exitCode;
    char *out = runClox(
        "fun isEven(x) { return x == 0 or x == 2 or x == 4 or x == 6 or x == 8; }\n"
        "var allFound = array_all([2, 4, 6, 8], isEven);\n"
        "print allFound;\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-all-finds-match: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "true\n")) {
        fail("stdlib/array-all-finds-match: expected 'true' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_all_no_match(void) {
    /* One element is falsy -> false (returns at that
     * element). The predicate is "isOdd" which only
     * matches {1, 3, 5}; [1, 3, 4, 5] has 4 which is not
     * in the set, so the result is false. */
    int exitCode;
    char *out = runClox(
        "fun isOdd(x) { return x == 1 or x == 3 or x == 5; }\n"
        "var allFound = array_all([1, 3, 4, 5], isOdd);\n"
        "print allFound;\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-all-no-match: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "false\n")) {
        fail("stdlib/array-all-no-match: expected 'false' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_all_empty(void) {
    /* Empty array -> true without ever calling the predicate.
     * Vacuously true: "all of zero things are true" — matches
     * JS Array.prototype.every and Python's all. */
    int exitCode;
    char *out = runClox(
        "fun isPositive(x) { return x > 0; }\n"
        "var allFound = array_all([], isPositive);\n"
        "print allFound;\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-all-empty: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "true\n")) {
        fail("stdlib/array-all-empty: expected 'true' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_all_short_circuits(void) {
    /* Short-circuit on the *first falsy*: once a falsy
     * element is found, the rest of the array is not
     * iterated. We test this with a side-effecting
     * predicate. If short-circuit works, only the elements
     * before (and including) the first falsy are printed. */
    int exitCode;
    char *out = runClox(
        "fun trace(x) { print \"visit:\" + string(x); return x != 3; }\n"
        "var allFound = array_all([1, 2, 3, 4, 5], trace);\n"
        "print allFound;\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-all-short-circuits: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "visit:1\n") || !contains(out, "visit:2\n")
            || !contains(out, "visit:3\n") || contains(out, "visit:4\n")) {
        fail("stdlib/array-all-short-circuits: expected short-circuit (no visit:4), got '%s'", out);
    } else if (!contains(out, "false\n")) {
        fail("stdlib/array-all-short-circuits: expected 'false', got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_all_single_element_truthy(void) {
    /* Single-element array where the element is truthy. */
    int exitCode;
    char *out = runClox(
        "fun isTruthy(x) { return x; }\n"
        "var allFound = array_all([42], isTruthy);\n"
        "print allFound;\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-all-single-truthy: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "true\n")) {
        fail("stdlib/array-all-single-truthy: expected 'true' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_all_single_element_falsy(void) {
    /* Single-element array where the predicate returns false.
     * The predicate isZero checks for x == 0; on [0] it
     * returns true (0 is "zero" semantically). On [1] it
     * returns false (1 is not zero), so the result is false.
     * (Mirrors the array_any-single-falsy test, with the
     * expected value flipped.) */
    int exitCode;
    char *out = runClox(
        "fun isNotZero(x) { return x != 0; }\n"
        "var allFound = array_all([1], isNotZero);\n"
        "print allFound;\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-all-single-falsy: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "true\n")) {
        fail("stdlib/array-all-single-falsy: expected 'true' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_all_does_not_mutate(void) {
    /* The source array is not mutated. */
    int exitCode;
    char *out = runClox(
        "var src = [10, 20, 30];\n"
        "fun isPositive(x) { return x > 0; }\n"
        "var allFound = array_all(src, isPositive);\n"
        "print \"len:\" + string(array_length(src));\n"
        "print \"0:\" + string(array_get(src, 0));\n"
        "print \"1:\" + string(array_get(src, 1));\n"
        "print \"2:\" + string(array_get(src, 2));\n"
        "print allFound;\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-all-does-not-mutate: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "len:3\n") || !contains(out, "0:10\n")
            || !contains(out, "1:20\n") || !contains(out, "2:30\n")
            || !contains(out, "true\n")) {
        fail("stdlib/array-all-does-not-mutate: expected src unchanged + true, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_all_wrong_arg_count(void) {
    /* 1 arg, 3 args, 0 args. */
    int exitCode;
    char *out = runClox(
        "fun isPositive(x) { return x > 0; }\n"
        "print array_all([1,2,3]);\n",
        &exitCode);
    if (exitCode == 0) {
        fail("stdlib/array-all-wrong-arg-count-one: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);

    out = runClox(
        "fun isPositive(x) { return x > 0; }\n"
        "print array_all([1,2,3], isPositive, 99);\n",
        &exitCode);
    if (exitCode == 0) {
        fail("stdlib/array-all-wrong-arg-count-three: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);

    out = runClox("print array_all();\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/array-all-wrong-arg-count-zero: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);
}

static void test_array_all_wrong_type(void) {
    /* Source must be array; predicate must be a 1-arg function. */
    int exitCode;
    char *out = runClox(
        "fun isPositive(x) { return x > 0; }\n"
        "print array_all(\"not an array\", isPositive);\n",
        &exitCode);
    if (exitCode == 0) {
        fail("stdlib/array-all-wrong-type-arr: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);

    out = runClox("print array_all([1,2,3], 42);\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/array-all-wrong-type-fn: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);

    out = runClox(
        "fun zeroArg() { return true; }\n"
        "print array_all([1,2,3], zeroArg);\n",
        &exitCode);
    if (exitCode == 0) {
        fail("stdlib/array-all-wrong-type-arity: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);
}

static void test_array_find_finds_match(void) {
    /* Basic case: returns the first element for which the
     * predicate is truthy. [2, 4, 5, 6] with isEven (which
     * matches {0, 2, 4, 6, 8}) returns 2 (the first match,
     * NOT 4 or 6). */
    int exitCode;
    char *out = runClox(
        "fun isEven(x) { return x == 0 or x == 2 or x == 4 or x == 6 or x == 8; }\n"
        "var found = array_find([2, 4, 5, 6], isEven);\n"
        "print found;\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-find-finds-match: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "2\n")) {
        fail("stdlib/array-find-finds-match: expected '2' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_find_no_match(void) {
    /* No element matches the predicate: returns nil. The
     * 'isOdd' predicate only matches {1, 3, 5}; [2, 4, 6, 8]
     * has no element in that set, so the result is nil.
     * The discipline: 'find(x) == nil' tests for "no match." */
    int exitCode;
    char *out = runClox(
        "fun isOdd(x) { return x == 1 or x == 3 or x == 5; }\n"
        "var found = array_find([2, 4, 6, 8], isOdd);\n"
        "print found;\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-find-no-match: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "nil\n")) {
        fail("stdlib/array-find-no-match: expected 'nil' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_find_empty(void) {
    /* Empty array: returns nil without ever calling the
     * predicate. (No iterations, no match.) */
    int exitCode;
    char *out = runClox(
        "fun isPositive(x) { return x > 0; }\n"
        "var found = array_find([], isPositive);\n"
        "print found;\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-find-empty: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "nil\n")) {
        fail("stdlib/array-find-empty: expected 'nil' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_find_short_circuits(void) {
    /* Short-circuit on the first truthy: once a match is
     * found, the rest of the array is not iterated. We
     * test this with a side-effecting predicate. If
     * short-circuit works, only the elements before (and
     * including) the first match are printed. */
    int exitCode;
    char *out = runClox(
        "fun trace(x) { print \"visit:\" + string(x); return x == 3; }\n"
        "var found = array_find([1, 2, 3, 4, 5], trace);\n"
        "print found;\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-find-short-circuits: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "visit:1\n") || !contains(out, "visit:2\n")
            || !contains(out, "visit:3\n") || contains(out, "visit:4\n")) {
        fail("stdlib/array-find-short-circuits: expected short-circuit (no visit:4), got '%s'", out);
    } else if (!contains(out, "3\n")) {
        fail("stdlib/array-find-short-circuits: expected '3' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_find_first_match_wins(void) {
    /* When multiple elements match, the first one wins.
     * [3, 5, 7, 9] with isOdd (which matches {1, 3, 5, 7, 9})
     * returns 3 (the first match), NOT 5, 7, or 9. */
    int exitCode;
    char *out = runClox(
        "fun isOdd(x) { return x == 1 or x == 3 or x == 5 or x == 7 or x == 9; }\n"
        "var found = array_find([3, 5, 7, 9], isOdd);\n"
        "print found;\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-find-first-match: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "3\n")) {
        fail("stdlib/array-find-first-match: expected '3' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_find_does_not_mutate(void) {
    /* The source array is not mutated. */
    int exitCode;
    char *out = runClox(
        "var src = [10, 20, 30, 40];\n"
        "fun isThirty(x) { return x == 30; }\n"
        "var found = array_find(src, isThirty);\n"
        "print found;\n"
        "print \"len:\" + string(array_length(src));\n"
        "print \"0:\" + string(array_get(src, 0));\n"
        "print \"3:\" + string(array_get(src, 3));\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-find-does-not-mutate: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "30\n") || !contains(out, "len:4\n")
            || !contains(out, "0:10\n") || !contains(out, "3:40\n")) {
        fail("stdlib/array-find-does-not-mutate: expected 30 + src unchanged, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_find_wrong_arg_count(void) {
    /* 1 arg, 3 args, 0 args. */
    int exitCode;
    char *out = runClox(
        "fun isPositive(x) { return x > 0; }\n"
        "print array_find([1,2,3]);\n",
        &exitCode);
    if (exitCode == 0) {
        fail("stdlib/array-find-wrong-arg-count-one: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);

    out = runClox(
        "fun isPositive(x) { return x > 0; }\n"
        "print array_find([1,2,3], isPositive, 99);\n",
        &exitCode);
    if (exitCode == 0) {
        fail("stdlib/array-find-wrong-arg-count-three: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);

    out = runClox("print array_find();\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/array-find-wrong-arg-count-zero: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);
}

static void test_array_find_wrong_type(void) {
    /* Source must be array; predicate must be a 1-arg function. */
    int exitCode;
    char *out = runClox(
        "fun isPositive(x) { return x > 0; }\n"
        "print array_find(\"not an array\", isPositive);\n",
        &exitCode);
    if (exitCode == 0) {
        fail("stdlib/array-find-wrong-type-arr: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);

    out = runClox("print array_find([1,2,3], 42);\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/array-find-wrong-type-fn: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);

    out = runClox(
        "fun zeroArg() { return true; }\n"
        "print array_find([1,2,3], zeroArg);\n",
        &exitCode);
    if (exitCode == 0) {
        fail("stdlib/array-find-wrong-type-arity: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);
}

static void test_array_find_index_finds_match(void) {
    /* Basic case: returns the index of the first element
     * for which the predicate is truthy. [10, 20, 30, 40]
     * with isThirty (which matches x == 30) returns 2
     * (the index of 30, NOT 1, 2, or 3). */
    int exitCode;
    char *out = runClox(
        "fun isThirty(x) { return x == 30; }\n"
        "var idx = array_find_index([10, 20, 30, 40], isThirty);\n"
        "print idx;\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-find-index-finds-match: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "2\n")) {
        fail("stdlib/array-find-index-finds-match: expected '2' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_find_index_no_match(void) {
    /* No element matches the predicate: returns -1 (the
     * canonical "no match" sentinel for index-based
     * searches in JS, Python, C). The 'isOdd' predicate
     * only matches {1, 3, 5}; [2, 4, 6, 8] has no element
     * in that set, so the result is -1. The discipline:
     * 'find_index(x) == -1' tests for "no match." */
    int exitCode;
    char *out = runClox(
        "fun isOdd(x) { return x == 1 or x == 3 or x == 5; }\n"
        "var idx = array_find_index([2, 4, 6, 8], isOdd);\n"
        "print idx;\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-find-index-no-match: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "-1\n")) {
        fail("stdlib/array-find-index-no-match: expected '-1' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_find_index_empty(void) {
    /* Empty array: returns -1 without ever calling the
     * predicate. (No iterations, no match.) */
    int exitCode;
    char *out = runClox(
        "fun isPositive(x) { return x > 0; }\n"
        "var idx = array_find_index([], isPositive);\n"
        "print idx;\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-find-index-empty: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "-1\n")) {
        fail("stdlib/array-find-index-empty: expected '-1' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_find_index_short_circuits(void) {
    /* Short-circuit on the first truthy: once a match is
     * found, the rest of the array is not iterated. We
     * test this with a side-effecting predicate. If
     * short-circuit works, only the elements before (and
     * including) the first match are printed. */
    int exitCode;
    char *out = runClox(
        "fun trace(x) { print \"visit:\" + string(x); return x == 30; }\n"
        "var idx = array_find_index([10, 20, 30, 40, 50], trace);\n"
        "print idx;\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-find-index-short-circuits: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "visit:10\n") || !contains(out, "visit:20\n")
            || !contains(out, "visit:30\n") || contains(out, "visit:40\n")) {
        fail("stdlib/array-find-index-short-circuits: expected short-circuit (no visit:40), got '%s'", out);
    } else if (!contains(out, "2\n")) {
        fail("stdlib/array-find-index-short-circuits: expected '2' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_find_index_first_match_wins(void) {
    /* When multiple elements match, the *first* one wins.
     * [3, 5, 7, 9] with isOdd (which matches {1, 3, 5, 7, 9})
     * returns 0 (the index of 3, the first match), NOT 1,
     * 2, or 3. */
    int exitCode;
    char *out = runClox(
        "fun isOdd(x) { return x == 1 or x == 3 or x == 5 or x == 7 or x == 9; }\n"
        "var idx = array_find_index([3, 5, 7, 9], isOdd);\n"
        "print idx;\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-find-index-first-match: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "0\n")) {
        fail("stdlib/array-find-index-first-match: expected '0' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_find_index_does_not_mutate(void) {
    /* The source array is not mutated. */
    int exitCode;
    char *out = runClox(
        "var src = [10, 20, 30, 40, 50];\n"
        "fun isThirty(x) { return x == 30; }\n"
        "var idx = array_find_index(src, isThirty);\n"
        "print idx;\n"
        "print \"len:\" + string(array_length(src));\n"
        "print \"0:\" + string(array_get(src, 0));\n"
        "print \"4:\" + string(array_get(src, 4));\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-find-index-does-not-mutate: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "2\n") || !contains(out, "len:5\n")
            || !contains(out, "0:10\n") || !contains(out, "4:50\n")) {
        fail("stdlib/array-find-index-does-not-mutate: expected 2 + src unchanged, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_find_index_wrong_arg_count(void) {
    /* 1 arg, 3 args, 0 args. */
    int exitCode;
    char *out = runClox(
        "fun isPositive(x) { return x > 0; }\n"
        "print array_find_index([1,2,3]);\n",
        &exitCode);
    if (exitCode == 0) {
        fail("stdlib/array-find-index-wrong-arg-count-one: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);

    out = runClox(
        "fun isPositive(x) { return x > 0; }\n"
        "print array_find_index([1,2,3], isPositive, 99);\n",
        &exitCode);
    if (exitCode == 0) {
        fail("stdlib/array-find-index-wrong-arg-count-three: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);

    out = runClox("print array_find_index();\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/array-find-index-wrong-arg-count-zero: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);
}

static void test_array_find_index_wrong_type(void) {
    /* Source must be array; predicate must be a 1-arg function. */
    int exitCode;
    char *out = runClox(
        "fun isPositive(x) { return x > 0; }\n"
        "print array_find_index(\"not an array\", isPositive);\n",
        &exitCode);
    if (exitCode == 0) {
        fail("stdlib/array-find-index-wrong-type-arr: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);

    out = runClox("print array_find_index([1,2,3], 42);\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/array-find-index-wrong-type-fn: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);

    out = runClox(
        "fun zeroArg() { return true; }\n"
        "print array_find_index([1,2,3], zeroArg);\n",
        &exitCode);
    if (exitCode == 0) {
        fail("stdlib/array-find-index-wrong-type-arity: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);
}

static void test_array_zip_basic(void) {
    /* Basic case: combines two arrays element-wise via
     * a 2-arg combiner. [1, 2, 3] and [10, 20, 30] with
     * combiner (a, b) -> a + b returns [11, 22, 33]. */
    int exitCode;
    char *out = runClox(
        "fun add(a, b) { return a + b; }\n"
        "var zipped = array_zip([1, 2, 3], [10, 20, 30], add);\n"
        "print \"len:\" + string(array_length(zipped));\n"
        "print \"0:\" + string(array_get(zipped, 0));\n"
        "print \"1:\" + string(array_get(zipped, 1));\n"
        "print \"2:\" + string(array_get(zipped, 2));\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-zip-basic: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "len:3\n") || !contains(out, "0:11\n")
            || !contains(out, "1:22\n") || !contains(out, "2:33\n")) {
        fail("stdlib/array-zip-basic: expected len:3 0:11 1:22 2:33, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_zip_truncates_to_shorter(void) {
    /* Different-length arrays: truncates to the shorter.
     * [1, 2, 3, 4, 5] and [10, 20] with combiner (a, b) -> a * b
     * returns [10, 40] (truncated to 2 elements). The discipline
     * matches Python's zip() and Rust's Iterator::zip(). */
    int exitCode;
    char *out = runClox(
        "fun mul(a, b) { return a * b; }\n"
        "var zipped = array_zip([1, 2, 3, 4, 5], [10, 20], mul);\n"
        "print \"len:\" + string(array_length(zipped));\n"
        "print \"0:\" + string(array_get(zipped, 0));\n"
        "print \"1:\" + string(array_get(zipped, 1));\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-zip-truncates: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "len:2\n") || !contains(out, "0:10\n")
            || !contains(out, "1:40\n")) {
        fail("stdlib/array-zip-truncates: expected len:2 0:10 1:40, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_zip_empty_arr1(void) {
    /* Empty arr1: returns empty array without ever
     * calling the combiner. */
    int exitCode;
    char *out = runClox(
        "fun add(a, b) { return a + b; }\n"
        "var zipped = array_zip([], [1, 2, 3], add);\n"
        "print \"len:\" + string(array_length(zipped));\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-zip-empty-arr1: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "len:0\n")) {
        fail("stdlib/array-zip-empty-arr1: expected len:0, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_zip_empty_arr2(void) {
    /* Empty arr2: returns empty array without ever
     * calling the combiner. */
    int exitCode;
    char *out = runClox(
        "fun add(a, b) { return a + b; }\n"
        "var zipped = array_zip([1, 2, 3], [], add);\n"
        "print \"len:\" + string(array_length(zipped));\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-zip-empty-arr2: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "len:0\n")) {
        fail("stdlib/array-zip-empty-arr2: expected len:0, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_zip_both_empty(void) {
    /* Both empty: returns empty array. */
    int exitCode;
    char *out = runClox(
        "fun add(a, b) { return a + b; }\n"
        "var zipped = array_zip([], [], add);\n"
        "print \"len:\" + string(array_length(zipped));\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-zip-both-empty: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "len:0\n")) {
        fail("stdlib/array-zip-both-empty: expected len:0, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_zip_does_not_mutate(void) {
    /* The source arrays are not mutated. */
    int exitCode;
    char *out = runClox(
        "var a1 = [1, 2, 3];\n"
        "var a2 = [10, 20, 30];\n"
        "fun add(a, b) { return a + b; }\n"
        "var zipped = array_zip(a1, a2, add);\n"
        "print \"a1.len:\" + string(array_length(a1));\n"
        "print \"a1.0:\" + string(array_get(a1, 0));\n"
        "print \"a1.2:\" + string(array_get(a1, 2));\n"
        "print \"a2.len:\" + string(array_length(a2));\n"
        "print \"a2.0:\" + string(array_get(a2, 0));\n"
        "print \"a2.2:\" + string(array_get(a2, 2));\n"
        "print \"z.len:\" + string(array_length(zipped));\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-zip-does-not-mutate: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "a1.len:3\n") || !contains(out, "a1.0:1\n")
            || !contains(out, "a1.2:3\n") || !contains(out, "a2.len:3\n")
            || !contains(out, "a2.0:10\n") || !contains(out, "a2.2:30\n")
            || !contains(out, "z.len:3\n")) {
        fail("stdlib/array-zip-does-not-mutate: expected srcs unchanged, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_zip_wrong_arg_count(void) {
    /* 1 arg, 2 args, 4 args, 0 args. */
    int exitCode;
    char *out = runClox(
        "fun add(a, b) { return a + b; }\n"
        "print array_zip([1,2,3]);\n",
        &exitCode);
    if (exitCode == 0) {
        fail("stdlib/array-zip-wrong-arg-count-one: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);

    out = runClox(
        "fun add(a, b) { return a + b; }\n"
        "print array_zip([1,2,3], [10,20,30]);\n",
        &exitCode);
    if (exitCode == 0) {
        fail("stdlib/array-zip-wrong-arg-count-two: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);

    out = runClox(
        "fun add(a, b) { return a + b; }\n"
        "print array_zip([1,2,3], [10,20,30], add, 99);\n",
        &exitCode);
    if (exitCode == 0) {
        fail("stdlib/array-zip-wrong-arg-count-four: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);

    out = runClox("print array_zip();\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/array-zip-wrong-arg-count-zero: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);
}

static void test_array_zip_wrong_type(void) {
    /* arr1 must be array; arr2 must be array; combiner must be 2-arg function. */
    int exitCode;
    char *out = runClox(
        "fun add(a, b) { return a + b; }\n"
        "print array_zip(\"not an array\", [1,2,3], add);\n",
        &exitCode);
    if (exitCode == 0) {
        fail("stdlib/array-zip-wrong-type-arr1: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);

    out = runClox(
        "fun add(a, b) { return a + b; }\n"
        "print array_zip([1,2,3], \"not an array\", add);\n",
        &exitCode);
    if (exitCode == 0) {
        fail("stdlib/array-zip-wrong-type-arr2: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);

    out = runClox("print array_zip([1,2,3], [10,20,30], 42);\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/array-zip-wrong-type-fn: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);

    out = runClox(
        "fun oneArg(a) { return a; }\n"
        "print array_zip([1,2,3], [10,20,30], oneArg);\n",
        &exitCode);
    if (exitCode == 0) {
        fail("stdlib/array-zip-wrong-type-arity: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);
}

static void test_array_flatten_basic(void) {
    /* Basic case: [[1, 2], [3, 4], [5, 6]] -> [1, 2, 3, 4, 5, 6].
     * Inner arrays' elements become top-level. The "shape
     * transform" pattern (nested -> flat). */
    int exitCode;
    char *out = runClox(
        "var flat = array_flatten([[1, 2], [3, 4], [5, 6]]);\n"
        "print \"len:\" + string(array_length(flat));\n"
        "print \"0:\" + string(array_get(flat, 0));\n"
        "print \"1:\" + string(array_get(flat, 1));\n"
        "print \"2:\" + string(array_get(flat, 2));\n"
        "print \"3:\" + string(array_get(flat, 3));\n"
        "print \"4:\" + string(array_get(flat, 4));\n"
        "print \"5:\" + string(array_get(flat, 5));\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-flatten-basic: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "len:6\n") || !contains(out, "0:1\n") || !contains(out, "1:2\n")
            || !contains(out, "2:3\n") || !contains(out, "3:4\n") || !contains(out, "4:5\n")
            || !contains(out, "5:6\n")) {
        fail("stdlib/array-flatten-basic: expected 1-6 in order, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_flatten_empty(void) {
    /* Empty array: returns empty array without iterating. */
    int exitCode;
    char *out = runClox(
        "var flat = array_flatten([]);\n"
        "print \"len:\" + string(array_length(flat));\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-flatten-empty: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "len:0\n")) {
        fail("stdlib/array-flatten-empty: expected 'len:0' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);

    /* Array containing only empty arrays: returns empty array. */
    out = runClox(
        "var flat = array_flatten([[], [], []]);\n"
        "print \"len:\" + string(array_length(flat));\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-flatten-empty-empty-arrays: expected exit 0, got %d", exitCode);
    } else if (!contains(out, "len:0\n")) {
        fail("stdlib/array-flatten-empty-empty-arrays: expected 'len:0' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_flatten_single_element_arrays(void) {
    /* Each inner array has exactly 1 element. Flattens
     * to a single array of those elements. */
    int exitCode;
    char *out = runClox(
        "var flat = array_flatten([[1], [2], [3], [4]]);\n"
        "print \"len:\" + string(array_length(flat));\n"
        "print \"0:\" + string(array_get(flat, 0));\n"
        "print \"1:\" + string(array_get(flat, 1));\n"
        "print \"2:\" + string(array_get(flat, 2));\n"
        "print \"3:\" + string(array_get(flat, 3));\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-flatten-single-element: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "len:4\n") || !contains(out, "0:1\n")
            || !contains(out, "1:2\n") || !contains(out, "2:3\n") || !contains(out, "3:4\n")) {
        fail("stdlib/array-flatten-single-element: expected 1-4 in order, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_flatten_mixed_types(void) {
    /* Mixed types: numbers, strings, booleans, nil, and
     * non-array elements. Non-array elements are pushed
     * as-is. Strings are still strings, booleans are still
     * booleans, nil is still nil. */
    int exitCode;
    char *out = runClox(
        "var flat = array_flatten([[1, \"two\"], [true, nil]]);\n"
        "print \"len:\" + string(array_length(flat));\n"
        "print \"0:\" + string(array_get(flat, 0));\n"
        "print array_get(flat, 1);\n"
        "print array_get(flat, 2);\n"
        "print array_get(flat, 3);\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-flatten-mixed-types: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "len:4\n") || !contains(out, "0:1\n")
            || !contains(out, "two\n") || !contains(out, "true\n") || !contains(out, "nil\n")) {
        fail("stdlib/array-flatten-mixed-types: expected 1, 'two', true, nil, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_flatten_does_not_recurse(void) {
    /* The "stop at 1 level" convention: nested arrays
     * (arrays inside inner arrays) are NOT recursed into;
     * they're pushed as-is. [[1, 2], [3, [4, 5]]] flattens
     * to [1, 2, 3, [4, 5]], NOT [1, 2, 3, 4, 5].
     *
     * Verification: array_length() on inner would error
     * if inner is not an array, so the fact that
     * inner_len:2 prints without error proves inner is
     * an array with 2 elements. (We don't use typeof()
     * here because typeof() doesn't yet recognize
     * OBJ_ARRAY — it returns "object" for arrays.) */
    int exitCode;
    char *out = runClox(
        "var flat = array_flatten([[1, 2], [3, [4, 5]]]);\n"
        "print \"len:\" + string(array_length(flat));\n"
        "var inner = array_get(flat, 3);\n"
        "print \"inner_len:\" + string(array_length(inner));\n"
        "print \"inner_0:\" + string(array_get(inner, 0));\n"
        "print \"inner_1:\" + string(array_get(inner, 1));\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-flatten-no-recurse: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "len:4\n") || !contains(out, "inner_len:2\n")
            || !contains(out, "inner_0:4\n") || !contains(out, "inner_1:5\n")) {
        fail("stdlib/array-flatten-no-recurse: expected 4 elements with [4,5] as last, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_flatten_preserves_source(void) {
    /* The source array is not mutated. */
    int exitCode;
    char *out = runClox(
        "var src = [[1, 2], [3, 4]];\n"
        "var flat = array_flatten(src);\n"
        "print \"flat_len:\" + string(array_length(flat));\n"
        "print \"src_len:\" + string(array_length(src));\n"
        "print \"src0_len:\" + string(array_length(array_get(src, 0)));\n"
        "print \"src0_0:\" + string(array_get(array_get(src, 0), 0));\n"
        "print \"src1_1:\" + string(array_get(array_get(src, 1), 1));\n",
        &exitCode);
    if (exitCode != 0) {
        fail("stdlib/array-flatten-preserves-source: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "flat_len:4\n") || !contains(out, "src_len:2\n")
            || !contains(out, "src0_len:2\n") || !contains(out, "src0_0:1\n") || !contains(out, "src1_1:4\n")) {
        fail("stdlib/array-flatten-preserves-source: expected flat=4 and src unchanged, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_array_flatten_wrong_arg_count(void) {
    /* 0 args, 2 args, 3 args. */
    int exitCode;
    char *out = runClox("print array_flatten();\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/array-flatten-wrong-arg-count-zero: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);

    out = runClox("print array_flatten([[1,2]], [[3,4]]);\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/array-flatten-wrong-arg-count-two: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);

    out = runClox("print array_flatten([[1,2]], [[3,4]], 99);\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/array-flatten-wrong-arg-count-three: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);
}

static void test_array_flatten_wrong_type(void) {
    /* Source must be array. */
    int exitCode;
    char *out = runClox("print array_flatten(\"not an array\");\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/array-flatten-wrong-type-string: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);

    out = runClox("print array_flatten(42);\n", &exitCode);
    if (exitCode == 0) {
        fail("stdlib/array-flatten-wrong-type-number: expected nonzero exit, got 0");
    } else {
        pass();
    }
    free(out);
}

int main(void) {
    test_clock_exists();
    test_number_abs();
    test_number_min_max();
    test_string_length();
    test_string_upper_lower();
    test_type_predicate();
    test_typeof_array();
    test_type_wrong_arg_count();
    /* Stage 8: more string operations. */
    test_string_substring();
    test_string_substring_clamp();
    test_string_contains();
    test_string_replace();
    test_string_replace_wrong_args();
    /* Stage 9: even more string operations. */
    test_string_starts_with();
    test_string_ends_with();
    test_string_index_of();
    test_string_trim();
    /* Stage 10: more number operations. */
    test_number_floor_ceil_round();
    test_number_sqrt();
    test_number_sqrt_negative();
    test_number_pow();
    test_number_wrong_args();
    /* Stage 11: I/O natives. */
    test_io_print();
    test_io_eprint();
    test_io_wrong_args();
    test_io_wrong_type();
    test_io_exit();
    test_io_read_line();
    /* Stage 12a: array value type via natives. */
    test_array_create_and_length();
    test_array_get_set();
    test_array_push();
    test_array_get_out_of_bounds();
    test_array_set_out_of_bounds();
    test_array_wrong_args();
    /* Stage 12b-i: array literals. */
    test_array_literal_3();
    test_array_literal_empty();
    test_array_literal_mixed();
    test_array_literal_nested();
    test_print_array_literal();
    /* Stage 12b-ii: a[i] index read. */
    test_array_index_read();
    test_array_index_nested();
    test_array_index_expression();
    test_array_index_out_of_bounds();
    test_array_index_negative();
    test_array_index_wrong_type();
    test_array_index_on_non_array();
    /* Stage 12b-iii: a[i] = v index write. */
    test_array_index_write();
    test_array_index_write_expression();
    test_array_index_write_then_read();
    test_array_index_write_oob();
    test_array_index_write_on_non_array();
    /* Stage 13: string_split / string_join. */
    test_string_split_basic();
    test_string_split_empty_delim();
    test_string_split_empty_string();
    test_string_split_no_match();
    test_string_split_multi_char();
    test_string_join_basic();
    test_string_join_empty_array();
    test_string_join_single_element();
    test_string_split_join_round_trip();
    test_string_split_gc_stress();
    test_string_split_wrong_type();
    test_string_join_wrong_type();
    test_string_join_non_string_element();
    /* Stage 14: more file I/O natives. */
    test_io_read_file_basic();
    test_io_read_file_nonexistent();
    test_io_read_file_empty();
    test_io_read_file_wrong_args();
    test_io_read_file_wrong_type();
    test_io_write_file_new();
    test_io_write_file_overwrite();
    test_io_write_file_empty();
    test_io_write_file_wrong_args();
    test_io_write_file_wrong_type();
    test_io_file_exists_true();
    test_io_file_exists_false();
    test_io_file_exists_wrong_args();
    test_io_file_exists_wrong_type();
    test_io_file_gc_stress();

    /* Stage 16: streaming I/O natives. */
    test_io_read_lines_basic();
    test_io_read_lines_no_trailing_newline();
    test_io_read_lines_empty_file();
    test_io_read_lines_nonexistent();
    test_io_read_lines_single_line();
    test_io_read_lines_wrong_args();
    test_io_read_lines_wrong_type();
    test_io_write_lines_new();
    test_io_write_lines_overwrite();
    test_io_write_lines_empty_array();
    test_io_write_lines_single_element();
    test_io_write_lines_wrong_args();
    test_io_write_lines_wrong_type();
    test_io_read_write_lines_round_trip();
    test_io_lines_gc_stress();

    /* Stage 17: number-to-string conversion. */
    test_string_int_positive();
    test_string_int_zero();
    test_string_int_negative();
    test_string_float();
    test_string_float_integer_valued();
    test_string_wrong_args();
    test_string_wrong_type();
    test_string_concat_pattern();
    test_string_gc_stress();

    /* Stage 19: array_reverse. */
    test_array_reverse_basic();
    test_array_reverse_returns_self();
    test_array_reverse_empty();
    test_array_reverse_single();
    test_array_reverse_two();
    test_array_reverse_even_length();
    test_array_reverse_strings();
    test_array_reverse_then_push();
    test_array_reverse_twice();
    test_array_reverse_wrong_arg_count();
    test_array_reverse_wrong_type();

    /* Stage 20: array_push returns the new length. */
    test_array_push_returns_new_length();
    test_array_push_returns_length_growing();
    test_array_push_return_value_is_number();
    test_array_push_chained_returns();

    /* Stage 21: string_repeat. */
    test_string_repeat_basic();
    test_string_repeat_zero();
    test_string_repeat_negative_errors();
    test_string_repeat_wrong_type();
    test_string_repeat_wrong_arg_count();

    /* Stage 22: string_pad_start. */
    test_string_pad_start_basic();
    test_string_pad_start_already_wide();
    test_string_pad_start_empty_s();
    test_string_pad_start_width_zero();
    test_string_pad_start_negative_errors();
    test_string_pad_start_empty_fill_errors();
    test_string_pad_start_wrong_type();
    test_string_pad_start_wrong_arg_count();
    /* Stage 23: string_pad_end. */
    test_string_pad_end_basic();
    test_string_pad_end_already_wide();
    test_string_pad_end_empty_s();
    test_string_pad_end_width_zero();
    test_string_pad_end_negative_errors();
    test_string_pad_end_empty_fill_errors();
    test_string_pad_end_wrong_type();
    test_string_pad_end_wrong_arg_count();
    /* Stage 24: string_to_number. */
    test_string_to_number_int_positive();
    test_string_to_number_int_negative();
    test_string_to_number_float();
    test_string_to_number_scientific();
    test_string_to_number_round_trip();
    test_string_to_number_empty_errors();
    test_string_to_number_non_numeric_errors();
    test_string_to_number_overflow_errors();
    test_string_to_number_wrong_type();
    test_string_to_number_wrong_arg_count();
    /* Stage 25: string_to_number with base. */
    test_string_to_number_base_decimal();
    test_string_to_number_base_binary();
    test_string_to_number_base_hex();
    test_string_to_number_base_auto();
    test_string_to_number_base_invalid();
    test_string_to_number_base_wrong_type();
    test_string_to_number_base_wrong_arg_count();
    /* Stage 26: string_to_int. */
    test_string_to_int_positive();
    test_string_to_int_negative();
    test_string_to_int_round_trip();
    test_string_to_int_float_errors();
    test_string_to_int_empty_errors();
    test_string_to_int_non_numeric_errors();
    test_string_to_int_wrong_type();
    test_string_to_int_wrong_arg_count();
    /* Stage 27: string_trim_start / string_trim_end. */
    test_string_trim_start_basic();
    test_string_trim_start_all_whitespace();
    test_string_trim_start_tabs_and_newlines();
    test_string_trim_start_wrong_type();
    test_string_trim_start_wrong_arg_count();
    test_string_trim_end_basic();
    test_string_trim_end_all_whitespace();
    test_string_trim_end_tabs_and_newlines();
    test_string_trim_end_wrong_type();
    test_string_trim_end_wrong_arg_count();
    test_string_trim_compose();
    /* Stage 28: array_unique. */
    test_array_unique_basic();
    test_array_unique_strings();
    test_array_unique_preserves_input();
    test_array_unique_empty();
    test_array_unique_single();
    test_array_unique_mixed_types();
    test_array_unique_wrong_arg_count();
    test_array_unique_wrong_type();
    /* Stage 40: array_unique_by with keyFn. */
    test_array_unique_by_basic();
    test_array_unique_by_preserves_order();
    test_array_unique_by_empty();
    test_array_unique_by_single();
    test_array_unique_by_does_not_mutate();
    test_array_unique_by_wrong_arg_count();
    test_array_unique_by_wrong_type();
    test_array_unique_by_typed_keys();
    /* Stage 41: array_chunk. */
    test_array_chunk_basic();
    test_array_chunk_evenly_divisible();
    test_array_chunk_size_one();
    test_array_chunk_size_equals_length();
    test_array_chunk_size_greater_than_length();
    test_array_chunk_empty();
    test_array_chunk_does_not_mutate();
    test_array_chunk_wrong_arg_count();
    test_array_chunk_wrong_type();
    test_array_chunk_size_zero();
    test_array_chunk_size_negative();
    /* Stage 42: array_group_by. */
    test_array_group_by_basic();
    test_array_group_by_preserves_order();
    test_array_group_by_empty();
    test_array_group_by_single();
    test_array_group_by_all_same_key();
    test_array_group_by_all_unique_keys();
    test_array_group_by_does_not_mutate();
    test_array_group_by_wrong_arg_count();
    test_array_group_by_wrong_type();
    test_array_group_by_three_groups();
    /* Stage 43: array_sort. */
    test_array_sort_basic();
    test_array_sort_already_sorted();
    test_array_sort_reverse();
    test_array_sort_empty();
    test_array_sort_single();
    test_array_sort_strings();
    test_array_sort_with_keyfn();
    test_array_sort_with_comparator();
    test_array_sort_does_not_mutate();
    test_array_sort_stable();
    test_array_sort_wrong_arg_count();
    test_array_sort_wrong_type();
    test_is_array_true();
    test_is_array_false();
    test_is_array_does_not_coerce();
    test_is_array_wrong_arg_count();
    /* Stage 29: string_split with limit. */
    test_string_split_with_limit();
    test_string_split_limit_zero();
    test_string_split_limit_one();
    test_string_split_limit_larger();
    test_string_split_limit_negative();
    test_string_split_limit_wrong_type();
    test_string_split_wrong_arg_count();

    /* Stage 30: array_filter. */
    test_array_filter_basic();
    test_array_filter_strings();
    test_array_filter_preserves_order();
    test_array_filter_empty();
    test_array_filter_all_filtered();
    test_array_filter_does_not_mutate();
    test_array_filter_wrong_arg_count();
    test_array_filter_wrong_type();

    /* Stage 31: array_map. */
    test_array_map_basic();
    test_array_map_type_change();
    test_array_map_preserves_order();
    test_array_map_empty();
    test_array_map_does_not_mutate();
    test_array_map_composes_with_filter();
    test_array_map_wrong_arg_count();
    test_array_map_wrong_type();

    /* Stage 32: array_reduce. */
    test_array_reduce_sum();
    test_array_reduce_product();
    test_array_reduce_string_concat();
    test_array_reduce_empty_array();
    test_array_reduce_type_change();
    test_array_reduce_composes_with_map();
    test_array_reduce_wrong_arg_count();
    test_array_reduce_wrong_type();
    /* Stage 33: array_any. */
    test_array_any_finds_match();
    test_array_any_no_match();
    test_array_any_empty();
    test_array_any_short_circuits();
    test_array_any_single_element_truthy();
    test_array_any_single_element_falsy();
    test_array_any_does_not_mutate();
    test_array_any_wrong_arg_count();
    test_array_any_wrong_type();
    /* Stage 34: array_all. */
    test_array_all_finds_match();
    test_array_all_no_match();
    test_array_all_empty();
    test_array_all_short_circuits();
    test_array_all_single_element_truthy();
    test_array_all_single_element_falsy();
    test_array_all_does_not_mutate();
    test_array_all_wrong_arg_count();
    test_array_all_wrong_type();
    /* Stage 35: array_find. */
    test_array_find_finds_match();
    test_array_find_no_match();
    test_array_find_empty();
    test_array_find_short_circuits();
    test_array_find_first_match_wins();
    test_array_find_does_not_mutate();
    test_array_find_wrong_arg_count();
    test_array_find_wrong_type();
    /* Stage 36: array_find_index. */
    test_array_find_index_finds_match();
    test_array_find_index_no_match();
    test_array_find_index_empty();
    test_array_find_index_short_circuits();
    test_array_find_index_first_match_wins();
    test_array_find_index_does_not_mutate();
    test_array_find_index_wrong_arg_count();
    test_array_find_index_wrong_type();
    /* Stage 37: array_zip. */
    test_array_zip_basic();
    test_array_zip_truncates_to_shorter();
    test_array_zip_empty_arr1();
    test_array_zip_empty_arr2();
    test_array_zip_both_empty();
    test_array_zip_does_not_mutate();
    test_array_zip_wrong_arg_count();
    test_array_zip_wrong_type();
    /* Stage 38: array_flatten. */
    test_array_flatten_basic();
    test_array_flatten_empty();
    test_array_flatten_single_element_arrays();
    test_array_flatten_mixed_types();
    test_array_flatten_does_not_recurse();
    test_array_flatten_preserves_source();
    test_array_flatten_wrong_arg_count();
    test_array_flatten_wrong_type();

    /* Stage 28: array_unique. (the duplicate block — was added by an
     * earlier session along with the Stage 29 ones; keeping it in place
     * would re-run all Stage 28 tests and skew the count. Remove.) */
    (void)0;

    printf("%d passed, %d failed\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}