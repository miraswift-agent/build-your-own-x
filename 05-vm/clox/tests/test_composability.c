#define _POSIX_C_SOURCE 200809L

/* Stage 15: composability tests.
 *
 * The point of these tests is NOT to add new natives — the stdlib is
 * stable. The point is to *demonstrate* that the existing primitives
 * compose into useful patterns, and to lock in those patterns with
 * tests so regressions get caught.
 *
 * The tests are intentionally cross-domain: each one composes at
 * least two of the major stdlib families (string, array, I/O, number).
 * If a future stage breaks any of these patterns, these tests will
 * fail and force the break to be acknowledged.
 *
 * Each test is a real program that a clox user could write. The
 * difference from `examples/` is that examples are docs and these
 * are tests: examples illustrate, tests verify. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>
#include <sys/wait.h>
#include <unistd.h>

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

static void pass(void) { g_passed++; }

static bool contains(const char* haystack, const char* needle) {
    return strstr(haystack, needle) != NULL;
}

static char* runClox(const char* source, int* exitCode) {
    char path[256];
    snprintf(path, sizeof(path), "/tmp/clox_test_comp_%d.lox", g_testCounter++);

    FILE* f = fopen(path, "w");
    if (f == NULL) { fprintf(stderr, "create failed\n"); exit(1); }
    fputs(source, f);
    fclose(f);

    char cmd[512];
    snprintf(cmd, sizeof(cmd), "%s/bin/clox %s 2>&1",
             "/home/mira/build-your-own-x/05-vm/clox", path);

    FILE* pipe = popen(cmd, "r");
    if (pipe == NULL) { fprintf(stderr, "popen failed\n"); exit(1); }

    size_t capacity = 256;
    size_t length = 0;
    char* output = (char*)malloc(capacity);
    if (output == NULL) exit(1);

    for (;;) {
        size_t remaining = capacity - length - 1;
        if (remaining < 64) {
            capacity *= 2;
            output = (char*)realloc(output, capacity);
            if (output == NULL) exit(1);
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

/* --- Composition 1: word count via split + length --- */

static void test_word_count_via_split(void) {
    /* Split a sentence into words, count them. A user-level pattern
     * that exercises split + array_length — no new natives needed. */
    int exitCode;
    char* out = runClox(
        "var sentence = \"the quick brown fox jumps over the lazy dog\";\n"
        "var words = string_split(sentence, \" \");\n"
        "print(array_length(words));\n",
        &exitCode);
    if (exitCode != 0) {
        fail("composability/word-count: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "9\n")) {
        fail("composability/word-count: expected 9, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

/* --- Composition 2: CSV round-trip --- */

static void test_csv_roundtrip(void) {
    /* Write a CSV, read it back, parse it, transform each field,
     * re-join, write the result. End-to-end exercise of file I/O
     * composing with split/join. Uses fixed /tmp paths and cleans
     * up after itself. */
    char pathA[256], pathB[256];
    snprintf(pathA, sizeof(pathA), "/tmp/clox_s15_csv_a_%d.txt", (int)getpid());
    snprintf(pathB, sizeof(pathB), "/tmp/clox_s15_csv_b_%d.txt", (int)getpid());
    unlink(pathA);
    unlink(pathB);

    char script[2048];
    snprintf(script, sizeof(script),
        "var a = \"%s\";\n"
        "var b = \"%s\";\n"
        "io_write_file(a, \"alpha,beta,gamma\");\n"
        "var csv = io_read_file(a);\n"
        "var fields = string_split(csv, \",\");\n"
        "fields[0] = string_upper(fields[0]);\n"
        "fields[1] = string_upper(fields[1]);\n"
        "fields[2] = string_upper(fields[2]);\n"
        "var upper = string_join(fields, \",\");\n"
        "io_write_file(b, upper);\n"
        "print(io_read_file(b));\n",
        pathA, pathB);

    int exitCode;
    char* out = runClox(script, &exitCode);
    if (exitCode != 0) {
        fail("composability/csv-roundtrip: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "ALPHA,BETA,GAMMA\n")) {
        fail("composability/csv-roundtrip: expected 'ALPHA,BETA,GAMMA' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
    unlink(pathA);
    unlink(pathB);
}

/* --- Composition 3: array filtering without a `filter` native --- */

static void test_array_filter_via_push(void) {
    /* Filter an array by walking it and pushing to a sink if the
     * predicate matches. No `filter` native exists — the user builds
     * the filter out of primitives.
     *
     * Note: clox's `/` is floating-point division, so a "is even"
     * check via `x / 2 * 2 == x` doesn't work. We use a simple
     * "greater than 5" predicate instead. The point of this test
     * is the filter *pattern*, not the predicate. */
    int exitCode;
    char* out = runClox(
        "var source = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10];\n"
        "var big = [];\n"
        "var i = 0;\n"
        "while (i < array_length(source)) {\n"
        "  if (source[i] > 5) {\n"
        "    array_push(big, source[i]);\n"
        "  }\n"
        "  i = i + 1;\n"
        "}\n"
        "print(array_length(big));\n"
        "print(big[0]);\n"
        "print(big[4]);\n",
        &exitCode);
    if (exitCode != 0) {
        fail("composability/array-filter: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "5\n") || !contains(out, "6\n") || !contains(out, "10\n")) {
        fail("composability/array-filter: expected 5, 6, 10 in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

/* --- Composition 4: file existence gates read --- */

static void test_io_file_exists_gates_read(void) {
    /* The standard "open if exists" pattern: check first, then read.
     * Demonstrates the I/O/programmer-error split from Stage 14. */
    char path[256];
    snprintf(path, sizeof(path), "/tmp/clox_s15_exists_%d.txt", (int)getpid());
    unlink(path);

    char script[1024];
    snprintf(script, sizeof(script),
        "var p = \"%s\";\n"
        "if (io_file_exists(p)) {\n"
        "  print io_read_file(p);\n"
        "} else {\n"
        "  print \"absent\";\n"
        "}\n"
        "io_write_file(p, \"present\");\n"
        "if (io_file_exists(p)) {\n"
        "  print io_read_file(p);\n"
        "} else {\n"
        "  print \"absent\";\n"
        "}\n",
        path);
    int exitCode;
    char* out = runClox(script, &exitCode);
    if (exitCode != 0) {
        fail("composability/exists-gates-read: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "absent\n") || !contains(out, "present\n")) {
        fail("composability/exists-gates-read: expected 'absent' and 'present' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
    unlink(path);
}

/* --- Composition 5: string ops chain --- */

static void test_string_ops_chain(void) {
    /* Take a messy input, trim it, split it, replace a piece,
     * re-join, and check the result. Five primitives in one
     * pipeline. */
    int exitCode;
    char* out = runClox(
        "var input = \"  apple,banana,cherry  \";\n"
        "var trimmed = string_trim(input);\n"
        "var parts = string_split(trimmed, \",\");\n"
        "parts[1] = string_replace(parts[1], \"banana\", \"BLUEBERRY\");\n"
        "var result = string_join(parts, \",\");\n"
        "print result;\n",
        &exitCode);
    if (exitCode != 0) {
        fail("composability/string-ops-chain: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "apple,BLUEBERRY,cherry\n")) {
        fail("composability/string-ops-chain: expected 'apple,BLUEBERRY,cherry' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

/* --- Composition 6: persistent state across "runs" --- */

static void test_persistent_state_counter(void) {
    /* Read a counter from disk, increment it, write it back. This
     * is the pattern that makes clox a *program* (with state)
     * rather than a script-runner. The test simulates a "second
     * run" by running two clox invocations sequentially. */
    char path[256];
    snprintf(path, sizeof(path), "/tmp/clox_s15_counter_%d.txt", (int)getpid());
    unlink(path);

    /* First "run": write 0. */
    char script1[1024];
    snprintf(script1, sizeof(script1),
        "var p = \"%s\";\n"
        "var n = 0;\n"
        "if (io_file_exists(p)) {\n"
        "  var contents = io_read_file(p);\n"
        "  n = string_length(contents);\n"
        "}\n"
        "n = n + 1;\n"
        "io_write_file(p, \"x\");\n"  /* file just needs to grow */
        "print n;\n",
        path);
    int exitCode;
    char* out = runClox(script1, &exitCode);
    int firstCount = -1;
    if (exitCode != 0 || !contains(out, "1\n")) {
        fail("composability/persistent-counter (run 1): expected '1', got exit %d output '%s'",
             exitCode, out);
        free(out);
        unlink(path);
        return;
    }
    free(out);

    /* Second "run": the file exists, so the contents length is 1
     * (we wrote "x" in run 1), so n becomes 1 + 1 = 2. We can't
     * parse ints from strings in pure clox (no number built-in for
     * string->int), but the increment pattern is what matters: the
     * state persists across runs. */
    char script2[1024];
    snprintf(script2, sizeof(script2),
        "var p = \"%s\";\n"
        "var n = 0;\n"
        "if (io_file_exists(p)) {\n"
        "  var contents = io_read_file(p);\n"
        "  n = string_length(contents);\n"
        "}\n"
        "n = n + 1;\n"
        "io_write_file(p, \"xx\");\n"
        "print n;\n",
        path);
    out = runClox(script2, &exitCode);
    if (exitCode != 0) {
        fail("composability/persistent-counter (run 2): expected exit 0, got %d (output: %s)",
             exitCode, out);
    } else if (!contains(out, "2\n")) {
        fail("composability/persistent-counter (run 2): expected '2', got '%s'", out);
    } else {
        pass();
    }
    free(out);
    unlink(path);
    (void)firstCount;  /* unused */
}

/* --- Composition 7: number ops + string formatting --- */

static void test_number_ops_chain(void) {
    /* Compute a number through multiple number operations, then
     * print it. Demonstrates that the number ops are composable.
     *
     * floor(3.7) = 3, pow(3, 3) = 27, sqrt(27) = 5.196..., round = 5.
     * So the output should contain '5' (from number_round) and
     * should NOT contain '3' (we don't print 3 directly). */
    int exitCode;
    char* out = runClox(
        "var x = 3.7;\n"
        "var y = number_floor(x);\n"
        "var z = number_pow(y, 3);\n"
        "var w = number_sqrt(z);\n"
        "print w;\n"
        "print number_round(w);\n",
        &exitCode);
    if (exitCode != 0) {
        fail("composability/number-ops-chain: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "5\n")) {
        fail("composability/number-ops-chain: expected '5' in output, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

/* --- Composition 8: split + sort-by-predicate + join --- */

static void test_split_reorder_join(void) {
    /* Take a comma-separated list, manually reverse it, and re-join.
     * Demonstrates array indexing composes with string_join. */
    int exitCode;
    char* out = runClox(
        "var csv = \"first,second,third,fourth\";\n"
        "var parts = string_split(csv, \",\");\n"
        "var n = array_length(parts);\n"
        "var reversed = [];\n"
        "var i = 0;\n"
        "while (i < n) {\n"
        "  array_push(reversed, parts[n - 1 - i]);\n"
        "  i = i + 1;\n"
        "}\n"
        "print string_join(reversed, \",\");\n",
        &exitCode);
    if (exitCode != 0) {
        fail("composability/split-reorder-join: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!contains(out, "fourth,third,second,first\n")) {
        fail("composability/split-reorder-join: expected 'fourth,third,second,first', got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

int main(void) {
    test_word_count_via_split();
    test_csv_roundtrip();
    test_array_filter_via_push();
    test_io_file_exists_gates_read();
    test_string_ops_chain();
    test_persistent_state_counter();
    test_number_ops_chain();
    test_split_reorder_join();

    printf("%d passed, %d failed\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
