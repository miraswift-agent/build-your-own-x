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

static char* runClox(const char* source, int* exitCode) {
    char path[256];
    snprintf(path, sizeof(path), "/tmp/clox_test_%d.lox", g_testCounter++);

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

static bool outputsEqual(const char* actual, const char* expected) {
    return strcmp(actual, expected) == 0;
}

/* --- Stage 18 tests: escape sequence processing in string literals.
 *
 * Before Stage 18: "a\nb" in source is the 2-char string backslash-n
 * (followed by 'a', 'b'). The user had to put a real newline in the
 * source to get a newline in the string.
 *
 * After Stage 18: "a\nb" in source is the 3-char string a-newline-b.
 * The supported escapes are: \n, \t, \r, \\, \". Unknown escapes
 * (e.g. \q) and incomplete escapes (a string ending in '\') are
 * compile errors. */

static void testEscapeNewline(void) {
    /* "a\nb" -> a, newline, b -> 3 chars. */
    int exitCode;
    char *out = runClox("print string_length(\"a\\nb\");\n", &exitCode);
    if (exitCode != 0) {
        fail("clox/escape-newline: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!outputsEqual(out, "3\n")) {
        fail("clox/escape-newline: expected '3', got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void testEscapeNewlineInPrint(void) {
    /* print "a\nb" should output a, newline, b (and the trailing
     * newline from print). So output is "a\nb\n". */
    int exitCode;
    char *out = runClox("print \"a\\nb\";\n", &exitCode);
    if (exitCode != 0) {
        fail("clox/escape-newline-print: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!outputsEqual(out, "a\nb\n")) {
        fail("clox/escape-newline-print: expected 'a\\nb\\n', got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void testEscapeTab(void) {
    /* "\t" -> 1 char (tab). */
    int exitCode;
    char *out = runClox("print string_length(\"\\t\");\n", &exitCode);
    if (exitCode != 0) {
        fail("clox/escape-tab: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!outputsEqual(out, "1\n")) {
        fail("clox/escape-tab: expected '1', got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void testEscapeCarriageReturn(void) {
    /* "\r" -> 1 char (CR). */
    int exitCode;
    char *out = runClox("print string_length(\"\\r\");\n", &exitCode);
    if (exitCode != 0) {
        fail("clox/escape-cr: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!outputsEqual(out, "1\n")) {
        fail("clox/escape-cr: expected '1', got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void testEscapeBackslash(void) {
    /* "\\\\" in source -> "\\" in C string -> one source char `\\` ->
     * one source char `\` -> \\ is the escape for `\ -> output 1 char. */
    int exitCode;
    char *out = runClox("print string_length(\"\\\\\");\n", &exitCode);
    if (exitCode != 0) {
        fail("clox/escape-backslash: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!outputsEqual(out, "1\n")) {
        fail("clox/escape-backslash: expected '1', got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void testEscapeBackslashInPrint(void) {
    /* "\\\\" in source -> output is "\". So print produces "\\n" (the
     * literal backslash followed by the print's newline). */
    int exitCode;
    char *out = runClox("print \"\\\\\";\n", &exitCode);
    if (exitCode != 0) {
        fail("clox/escape-backslash-print: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!outputsEqual(out, "\\\n")) {
        fail("clox/escape-backslash-print: expected '\\\\\\n', got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void testEscapeDoubleQuote(void) {
    /* "\"" in source -> 1 char ("). And we can embed " inside a string
     * without ending it. */
    int exitCode;
    char *out = runClox("print string_length(\"\\\"\");\n", &exitCode);
    if (exitCode != 0) {
        fail("clox/escape-double-quote: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!outputsEqual(out, "1\n")) {
        fail("clox/escape-double-quote: expected '1', got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void testEscapeEmbeddedQuote(void) {
    /* "a\"b" -> a, ", b -> 3 chars. The string contains a literal ". */
    int exitCode;
    char *out = runClox(
        "var s = \"a\\\"b\";\n"
        "print string_length(s);\n"
        "print s;\n",
        &exitCode);
    if (exitCode != 0) {
        fail("clox/escape-embedded-quote: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!outputsEqual(out, "3\na\"b\n")) {
        fail("clox/escape-embedded-quote: expected '3\\na\"b\\n', got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void testEscapeMixed(void) {
    /* "a\nb\tc\\d\"e" -> a, NL, b, TAB, c, \, d, ", e -> 9 chars. */
    int exitCode;
    char *out = runClox("print string_length(\"a\\nb\\tc\\\\d\\\"e\");\n", &exitCode);
    if (exitCode != 0) {
        fail("clox/escape-mixed: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!outputsEqual(out, "9\n")) {
        fail("clox/escape-mixed: expected '9', got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void testEscapeUnknown(void) {
    /* "\q" is not a known escape -> compile error (exit 65). */
    int exitCode;
    char *out = runClox("var s = \"\\q\";\n", &exitCode);
    if (exitCode != 65) {
        fail("clox/escape-unknown: expected exit 65, got %d (output: %s)", exitCode, out);
    } else {
        pass();
    }
    free(out);
}

static void testEscapeIncomplete(void) {
    /* String ending in '\' (no escape char after) -> compile error. */
    int exitCode;
    char *out = runClox("var s = \"foo\\\";\n", &exitCode);
    if (exitCode != 65) {
        fail("clox/escape-incomplete: expected exit 65, got %d (output: %s)", exitCode, out);
    } else {
        pass();
    }
    free(out);
}

static void testEscapeNoLongerNeedsRealNewline(void) {
    /* The Stage 15 workaround: put a real newline in source to get a
     * newline in the string. After Stage 18, "\n" works. This test
     * confirms the new idiom produces the same result as the old one
     * would have. */
    int exitCode;
    char *out = runClox("var a = \"line1\\nline2\";\nprint a;\n", &exitCode);
    if (exitCode != 0) {
        fail("clox/escape-no-real-newline: expected exit 0, got %d (output: %s)", exitCode, out);
    } else if (!outputsEqual(out, "line1\nline2\n")) {
        fail("clox/escape-no-real-newline: expected 'line1\\nline2\\n', got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void testArithmetic(void) {
    int exitCode;
    char* out = runClox("print 1 + 2 * 3 - 4 / 2;", &exitCode);
    if (exitCode != 0) {
        fail("arithmetic: expected exit 0, got %d", exitCode);
    } else if (!outputsEqual(out, "5\n")) {
        fail("arithmetic: expected '5\\n', got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void testComparison(void) {
    int exitCode;
    char* out = runClox(
        "print 1 < 2;\n"
        "print \"a\" == \"a\";\n"
        "print 1 != 2;",
        &exitCode);
    if (exitCode != 0) {
        fail("comparison: expected exit 0, got %d", exitCode);
    } else if (!outputsEqual(out, "true\ntrue\ntrue\n")) {
        fail("comparison: expected three trues, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void testVariables(void) {
    int exitCode;
    char* out = runClox(
        "var x = 1;\n"
        "x = 2;\n"
        "print x;",
        &exitCode);
    if (exitCode != 0) {
        fail("variables: expected exit 0, got %d", exitCode);
    } else if (!outputsEqual(out, "2\n")) {
        fail("variables: expected '2\\n', got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void testIfElse(void) {
    int exitCode;
    char* out = runClox(
        "if (true) print \"yes\"; else print \"no\";\n"
        "if (false) print \"bad\"; else print \"ok\";",
        &exitCode);
    if (exitCode != 0) {
        fail("if/else: expected exit 0, got %d", exitCode);
    } else if (!outputsEqual(out, "yes\nok\n")) {
        fail("if/else: expected 'yes\\nok\\n', got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void testWhile(void) {
    int exitCode;
    char* out = runClox(
        "var sum = 0;\n"
        "var i = 1;\n"
        "while (i <= 10) {\n"
        "    sum = sum + i;\n"
        "    i = i + 1;\n"
        "}\n"
        "print sum;",
        &exitCode);
    if (exitCode != 0) {
        fail("while: expected exit 0, got %d", exitCode);
    } else if (!outputsEqual(out, "55\n")) {
        fail("while: expected '55\\n', got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void testFor(void) {
    int exitCode;
    char* out = runClox(
        "var sum = 0;\n"
        "for (var i = 1; i <= 10; i = i + 1) {\n"
        "    sum = sum + i;\n"
        "}\n"
        "print sum;",
        &exitCode);
    if (exitCode != 0) {
        fail("for: expected exit 0, got %d", exitCode);
    } else if (!outputsEqual(out, "55\n")) {
        fail("for: expected '55\\n', got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void testFunction(void) {
    int exitCode;
    char* out = runClox(
        "fun add(a, b) {\n"
        "    return a + b;\n"
        "}\n"
        "print add(2, 3);",
        &exitCode);
    if (exitCode != 0) {
        fail("function: expected exit 0, got %d", exitCode);
    } else if (!outputsEqual(out, "5\n")) {
        fail("function: expected '5\\n', got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void testRecursion(void) {
    int exitCode;
    char* out = runClox(
        "fun fib(n) {\n"
        "    if (n < 2) return n;\n"
        "    return fib(n - 1) + fib(n - 2);\n"
        "}\n"
        "print fib(10);",
        &exitCode);
    if (exitCode != 0) {
        fail("recursion: expected exit 0, got %d", exitCode);
    } else if (!outputsEqual(out, "55\n")) {
        fail("recursion: expected '55\\n', got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void testClosure(void) {
    int exitCode;
    char* out = runClox(
        "fun makeCounter() {\n"
        "    var i = 0;\n"
        "    fun count() {\n"
        "        i = i + 1;\n"
        "        return i;\n"
        "    }\n"
        "    return count;\n"
        "}\n"
        "var c = makeCounter();\n"
        "print c();\n"
        "print c();\n"
        "print c();",
        &exitCode);
    if (exitCode != 0) {
        fail("closure: expected exit 0, got %d", exitCode);
    } else if (!outputsEqual(out, "1\n2\n3\n")) {
        fail("closure: expected '1\\n2\\n3\\n', got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void testClass(void) {
    int exitCode;
    char* out = runClox(
        "class Point {\n"
        "    init(x, y) {\n"
        "        this.x = x;\n"
        "        this.y = y;\n"
        "    }\n"
        "    sum() {\n"
        "        return this.x + this.y;\n"
        "    }\n"
        "}\n"
        "var p = Point(3, 4);\n"
        "print p.sum();",
        &exitCode);
    if (exitCode != 0) {
        fail("class: expected exit 0, got %d", exitCode);
    } else if (!outputsEqual(out, "7\n")) {
        fail("class: expected '7\\n', got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void testInheritance(void) {
    int exitCode;
    char* out = runClox(
        "class A {\n"
        "    foo() {\n"
        "        return 1;\n"
        "    }\n"
        "}\n"
        "class B < A {\n"
        "    foo() {\n"
        "        return super.foo() + 1;\n"
        "    }\n"
        "}\n"
        "print B().foo();",
        &exitCode);
    if (exitCode != 0) {
        fail("inheritance: expected exit 0, got %d", exitCode);
    } else if (!outputsEqual(out, "2\n")) {
        fail("inheritance: expected '2\\n', got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void testSyntaxError(void) {
    int exitCode;
    char* out = runClox("var x =;", &exitCode);
    if (exitCode != 65) {
        fail("syntax error: expected exit 65, got %d", exitCode);
    } else {
        pass();
    }
    free(out);
}

static void testRuntimeError(void) {
    int exitCode;
    char* out = runClox("nil + 1;", &exitCode);
    if (exitCode != 70) {
        fail("runtime error: expected exit 70, got %d", exitCode);
    } else {
        pass();
    }
    free(out);
}

static void testBlockScope(void) {
    int exitCode;
    char* out = runClox(
        "var x = 1;\n"
        "{\n"
        "    var x = 2;\n"
        "}\n"
        "print x;",
        &exitCode);
    if (exitCode != 0) {
        fail("block scope: expected exit 0, got %d", exitCode);
    } else if (!outputsEqual(out, "1\n")) {
        fail("block scope: expected '1\\n', got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void testGCStress(void) {
    int exitCode;
    char* out = runClox(
        "fun f() { return \"a\" + \"b\"; }\n"
        "var i = 0;\n"
        "while (i < 10000) { f(); i = i + 1; }\n"
        "print \"ok\";",
        &exitCode);
    if (exitCode != 0) {
        fail("gc stress: expected exit 0, got %d", exitCode);
    } else if (!outputsEqual(out, "ok\n")) {
        fail("gc stress: expected 'ok\\n', got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

int main(void) {
    testArithmetic();
    testComparison();
    testVariables();
    testIfElse();
    testWhile();
    testFor();
    testFunction();
    testRecursion();
    testClosure();
    testClass();
    testInheritance();
    testSyntaxError();
    testRuntimeError();
    testBlockScope();
    testGCStress();

    /* Stage 18: escape sequence processing in string literals. */
    testEscapeNewline();
    testEscapeNewlineInPrint();
    testEscapeTab();
    testEscapeCarriageReturn();
    testEscapeBackslash();
    testEscapeBackslashInPrint();
    testEscapeDoubleQuote();
    testEscapeEmbeddedQuote();
    testEscapeMixed();
    testEscapeUnknown();
    testEscapeIncomplete();
    testEscapeNoLongerNeedsRealNewline();

    printf("%d passed, %d failed\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
