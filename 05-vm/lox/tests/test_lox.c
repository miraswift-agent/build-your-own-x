#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>
#include <ctype.h>
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

static char* runLox(const char* source, int* exitCode) {
    char path[256];
    snprintf(path, sizeof(path), "/tmp/lox_test_%d.lox", g_testCounter++);

    FILE* f = fopen(path, "w");
    if (f == NULL) {
        fprintf(stderr, "Failed to create temp file.\n");
        exit(1);
    }
    fputs(source, f);
    fclose(f);

    char cmd[512];
    snprintf(cmd, sizeof(cmd), "./bin/lox %s 2>&1", path);

    FILE* pipe = popen(cmd, "r");
    if (pipe == NULL) {
        fprintf(stderr, "Failed to run lox.\n");
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

static void testArithmetic(void) {
    int exitCode;
    char* out = runLox("print 1 + 2 * 3 - 4 / 2;", &exitCode);
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
    char* out = runLox(
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
    char* out = runLox(
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
    char* out = runLox(
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
    char* out = runLox(
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
    char* out = runLox(
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
    char* out = runLox(
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
    char* out = runLox(
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
    char* out = runLox(
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
    char* out = runLox(
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
    char* out = runLox(
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
    char* out = runLox("var x =;", &exitCode);
    if (exitCode != 65) {
        fail("syntax error: expected exit 65, got %d", exitCode);
    } else {
        pass();
    }
    free(out);
}

static void testRuntimeError(void) {
    int exitCode;
    char* out = runLox("nil + 1;", &exitCode);
    if (exitCode != 70) {
        fail("runtime error: expected exit 70, got %d", exitCode);
    } else {
        pass();
    }
    free(out);
}

static void testBlockScope(void) {
    int exitCode;
    char* out = runLox(
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

    printf("%d passed, %d failed\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
