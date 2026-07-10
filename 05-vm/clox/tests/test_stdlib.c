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

    printf("%d passed, %d failed\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}