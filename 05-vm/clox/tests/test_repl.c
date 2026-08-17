/*
 * clox — REPL integration tests
 *
 * These tests drive `./bin/clox --repl` as a subprocess via popen, feed it
 * a sequence of input lines on stdin, and assert on the combined stdout+stderr
 * output. They exist to lock in the contract that the REPL is interactive
 * AND stateful AND can accept multi-line input AND recovers from errors
 * without exiting.
 *
 * Style mirrors tests/test_clox.c — popen-based subprocess driver, simple
 * pass/fail counters, exit 0 only when every test passes.
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>
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

/*
 * Drive the REPL with a sequence of newline-terminated lines on stdin.
 * Captures the combined stdout+stderr output. Returns the exit code of
 * the clox process (0 on clean EOF from stdin).
 *
 * The caller is responsible for freeing the returned buffer.
 */
static char* runCloxRepl(const char* lines[], int numLines, int* exitCode) {
    /* Write the input to a temp file so we can pipe it in via shell. */
    char inPath[256];
    snprintf(inPath, sizeof(inPath), "/tmp/clox_repl_in_%d.lox", g_testCounter++);

    FILE* inFile = fopen(inPath, "w");
    if (inFile == NULL) {
        fprintf(stderr, "Failed to create temp input file.\n");
        exit(1);
    }
    for (int i = 0; i < numLines; i++) {
        fputs(lines[i], inFile);
    }
    fclose(inFile);

    char cmd[512];
    snprintf(cmd, sizeof(cmd), "./bin/clox --repl < %s 2>&1", inPath);

    FILE* pipe = popen(cmd, "r");
    if (pipe == NULL) {
        fprintf(stderr, "Failed to run clox --repl.\n");
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
    remove(inPath);
    return output;
}

/* Returns true if `needle` is found as a substring of `haystack`. */
static bool contains(const char* haystack, const char* needle) {
    return strstr(haystack, needle) != NULL;
}

/* --- The five tests --- */

static void test_repl_single_line(void) {
    const char* input[] = { "print 1 + 2;\n" };
    int exitCode;
    char* out = runCloxRepl(input, 1, &exitCode);
    if (exitCode != 0) {
        fail("repl/single-line: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "3")) {
        fail("repl/single-line: expected output to contain '3', got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_repl_state_persists(void) {
    /* Define a var, then use it on the next REPL input. This is the
     * bug-1 test: previously the second compile was poisoned by parser
     * state from the first and produced "Expect ';' after value." */
    const char* input[] = {
        "var x = 42;\n",
        "print x;\n",
    };
    int exitCode;
    char* out = runCloxRepl(input, 2, &exitCode);
    if (exitCode != 0) {
        fail("repl/state-persists: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "42")) {
        fail("repl/state-persists: expected output to contain '42', got '%s'", out);
    } else if (contains(out, "Expect ';' after value.")) {
        fail("repl/state-persists: parser state leaked between inputs, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_repl_multiline(void) {
    /* A for-loop spread across three REPL inputs. The REPL must
     * accumulate until the closing brace. */
    const char* input[] = {
        "for (var i = 0; i < 3; i = i + 1) {\n",
        "  print i;\n",
        "}\n",
    };
    int exitCode;
    char* out = runCloxRepl(input, 3, &exitCode);
    if (exitCode != 0) {
        fail("repl/multiline: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "0") || !contains(out, "1") || !contains(out, "2")) {
        fail("repl/multiline: expected output to contain 0, 1, and 2, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_repl_function_def_then_call(void) {
    /* Define a function across multiple REPL lines, then call it. */
    const char* input[] = {
        "fun add(a, b) {\n",
        "  return a + b;\n",
        "}\n",
        "print add(2, 3);\n",
    };
    int exitCode;
    char* out = runCloxRepl(input, 4, &exitCode);
    if (exitCode != 0) {
        fail("repl/function-def: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "5")) {
        fail("repl/function-def: expected output to contain '5', got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void test_repl_runtime_error_recovery(void) {
    /* A runtime error on one line must not kill the REPL — the next
     * input must still execute. The error output must contain a stack
     * trace (look for the `[line` marker that runtimeError prints). */
    const char* input[] = {
        "print nil + 1;\n",
        "print \"still here\";\n",
    };
    int exitCode;
    char* out = runCloxRepl(input, 2, &exitCode);
    if (exitCode != 0) {
        fail("repl/runtime-error-recovery: expected exit 0 (REPL survives), got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "still here")) {
        fail("repl/runtime-error-recovery: expected 'still here' in output, got '%s'", out);
    } else if (!contains(out, "[line")) {
        fail("repl/runtime-error-recovery: expected stack trace marker '[line' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

int main(void) {
    test_repl_single_line();
    test_repl_state_persists();
    test_repl_multiline();
    test_repl_function_def_then_call();
    test_repl_runtime_error_recovery();

    printf("%d passed, %d failed\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
