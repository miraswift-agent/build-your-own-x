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

    printf("%d passed, %d failed\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}