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

int main(void) {
    test_clock_exists();
    test_number_abs();
    test_number_min_max();
    test_string_length();
    test_string_upper_lower();
    test_type_predicate();
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

    printf("%d passed, %d failed\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}