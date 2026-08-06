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

/* Stage 64.2 — multi-file import fixtures. */
static char* runCloxPath(const char* path, int* exitCode) {
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
    return output;
}

static void writeFile(const char* path, const char* contents) {
    FILE* f = fopen(path, "w");
    if (f == NULL) {
        fprintf(stderr, "Failed to write %s\n", path);
        exit(1);
    }
    fputs(contents, f);
    fclose(f);
}

static void testImportBasic(void) {
    system("mkdir -p /tmp/clox_mod_fixture");
    writeFile("/tmp/clox_mod_fixture/mathutil.lox",
              "fun add(a, b) { return a + b; }\n"
              "var PI = 3.14;\n");
    writeFile("/tmp/clox_mod_fixture/main.lox",
              "import \"mathutil.lox\" as math;\n"
              "print math.add(1, 2);\n"
              "print math.PI;\n"
              "print typeof(math);\n");
    int exitCode;
    char* out = runCloxPath("/tmp/clox_mod_fixture/main.lox", &exitCode);
    if (exitCode != 0) {
        fail("import basic: expected exit 0, got %d (%s)", exitCode, out);
    } else if (!outputsEqual(out, "3\n3.14\nmodule\n")) {
        fail("import basic: unexpected output '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void testImportCacheIdentity(void) {
    writeFile("/tmp/clox_mod_fixture/cache_main.lox",
              "import \"mathutil.lox\" as a;\n"
              "import \"mathutil.lox\" as b;\n"
              "print a == b;\n");
    int exitCode;
    char* out = runCloxPath("/tmp/clox_mod_fixture/cache_main.lox", &exitCode);
    if (exitCode != 0) {
        fail("import cache: expected exit 0, got %d (%s)", exitCode, out);
    } else if (!outputsEqual(out, "true\n")) {
        fail("import cache: expected 'true\n', got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

static void testImportTopLevelOnly(void) {
    writeFile("/tmp/clox_mod_fixture/nested_bad.lox",
              "fun f() {\n"
              "  import \"mathutil.lox\" as m;\n"
              "}\n");
    int exitCode;
    char* out = runCloxPath("/tmp/clox_mod_fixture/nested_bad.lox", &exitCode);
    if (exitCode != 65) {
        fail("import top-level: expected exit 65, got %d (%s)", exitCode, out);
    } else if (strstr(out, "Can only import at top level") == NULL) {
        fail("import top-level: missing error text, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

/* Stage 64.4 — mutual import with functions only (cycle-safe init order). */
static void testImportCycleFunctionsOk(void) {
    system("mkdir -p /tmp/clox_mod_fixture");
    writeFile("/tmp/clox_mod_fixture/cycle_a.lox",
              "import \"cycle_b.lox\" as b;\n"
              "fun ping() { return b.pong(); }\n");
    writeFile("/tmp/clox_mod_fixture/cycle_b.lox",
              "import \"cycle_a.lox\" as a;\n"
              "fun pong() { return 42; }\n");
    writeFile("/tmp/clox_mod_fixture/cycle_main.lox",
              "import \"cycle_a.lox\" as a;\n"
              "print a.ping();\n");
    int exitCode;
    char* out = runCloxPath("/tmp/clox_mod_fixture/cycle_main.lox", &exitCode);
    if (exitCode != 0) {
        fail("import cycle funs: expected exit 0, got %d (%s)", exitCode, out);
    } else if (!outputsEqual(out, "42\n")) {
        fail("import cycle funs: unexpected output '%s'", out);
    } else {
        pass();
    }
    free(out);
}

/* Stage 64.4 — reading peer export while peer is still LOADING → loud error. */
static void testImportCycleLoadingGetError(void) {
    writeFile("/tmp/clox_mod_fixture/load_a.lox",
              "import \"load_b.lox\" as b;\n"
              "var ready = true;\n");
    writeFile("/tmp/clox_mod_fixture/load_b.lox",
              "import \"load_a.lox\" as a;\n"
              "print a.ready;\n"
              "var done = true;\n");
    writeFile("/tmp/clox_mod_fixture/load_main.lox",
              "import \"load_a.lox\" as a;\n"
              "print a.ready;\n");
    int exitCode;
    char* out = runCloxPath("/tmp/clox_mod_fixture/load_main.lox", &exitCode);
    if (exitCode == 0) {
        fail("import cycle loading: expected runtime error, got ok (%s)", out);
    } else if (strstr(out, "still loading") == NULL) {
        fail("import cycle loading: missing 'still loading', got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

/* Stage 64.4 — missing module path. */
static void testImportMissingFile(void) {
    writeFile("/tmp/clox_mod_fixture/missing_main.lox",
              "import \"no_such_module.lox\" as m;\n");
    int exitCode;
    char* out = runCloxPath("/tmp/clox_mod_fixture/missing_main.lox", &exitCode);
    if (exitCode == 0) {
        fail("import missing: expected error, got ok");
    } else if (strstr(out, "Could not load module") == NULL) {
        fail("import missing: wrong error text '%s'", out);
    } else {
        pass();
    }
    free(out);
}

/* Stage 64.5 — entry script is a module; peer can import it without re-run. */
static void testMainAsModuleCycle(void) {
    system("mkdir -p /tmp/clox_mod_fixture");
    writeFile("/tmp/clox_mod_fixture/mam_main.lox",
              "import \"mam_back.lox\" as b;\n"
              "var mark = 7;\n"
              "print b.see();\n");
    writeFile("/tmp/clox_mod_fixture/mam_back.lox",
              "import \"mam_main.lox\" as m;\n"
              "fun see() { return m.mark; }\n");
    int exitCode;
    char* out = runCloxPath("/tmp/clox_mod_fixture/mam_main.lox", &exitCode);
    if (exitCode != 0) {
        fail("main-as-module: expected exit 0, got %d (%s)", exitCode, out);
    } else if (!outputsEqual(out, "7\n")) {
        fail("main-as-module: unexpected output '%s'", out);
    } else {
        pass();
    }
    free(out);
}

/* Stage 64.5 — entry top-level still callable after load via export. */
static void testMainAsModuleExport(void) {
    writeFile("/tmp/clox_mod_fixture/mam_lib.lox",
              "fun hi() { return 99; }\n");
    writeFile("/tmp/clox_mod_fixture/mam_entry.lox",
              "import \"mam_lib.lox\" as lib;\n"
              "fun wrap() { return lib.hi(); }\n"
              "print wrap();\n"
              "print typeof(wrap);\n");
    int exitCode;
    char* out = runCloxPath("/tmp/clox_mod_fixture/mam_entry.lox", &exitCode);
    if (exitCode != 0) {
        fail("main export: expected exit 0, got %d (%s)", exitCode, out);
    } else if (!outputsEqual(out, "99\nfunction\n")) {
        fail("main export: unexpected output '%s'", out);
    } else {
        pass();
    }
    free(out);
}

/* Stage 64.6 — selective import binds exports into importer globals. */
static void testSelectiveImportBasic(void) {
    system("mkdir -p /tmp/clox_mod_fixture");
    writeFile("/tmp/clox_mod_fixture/mathutil.lox",
              "fun add(a, b) { return a + b; }\n"
              "var PI = 3.14;\n"
              "var secret = 99;\n");
    writeFile("/tmp/clox_mod_fixture/sel_main.lox",
              "import { add, PI } from \"mathutil.lox\";\n"
              "print add(2, 3);\n"
              "print PI;\n");
    int exitCode;
    char* out = runCloxPath("/tmp/clox_mod_fixture/sel_main.lox", &exitCode);
    if (exitCode != 0) {
        fail("selective import: expected exit 0, got %d (%s)", exitCode, out);
    } else if (!outputsEqual(out, "5\n3.14\n")) {
        fail("selective import: unexpected output '%s'", out);
    } else {
        pass();
    }
    free(out);
}

/* Stage 64.6 — unbound names stay unbound (no silent whole-module dump). */
static void testSelectiveImportDoesNotBindOthers(void) {
    writeFile("/tmp/clox_mod_fixture/sel_secret.lox",
              "import { add } from \"mathutil.lox\";\n"
              "print secret;\n");
    int exitCode;
    char* out = runCloxPath("/tmp/clox_mod_fixture/sel_secret.lox", &exitCode);
    if (exitCode == 0) {
        fail("selective unbound: expected runtime error, got ok (%s)", out);
    } else if (strstr(out, "Undefined variable") == NULL &&
               strstr(out, "undefined") == NULL &&
               strstr(out, "Undefined") == NULL) {
        fail("selective unbound: wrong error '%s'", out);
    } else {
        pass();
    }
    free(out);
}

/* Stage 64.6 — missing export name is a runtime property error. */
static void testSelectiveImportMissingExport(void) {
    writeFile("/tmp/clox_mod_fixture/sel_missing.lox",
              "import { nope } from \"mathutil.lox\";\n");
    int exitCode;
    char* out = runCloxPath("/tmp/clox_mod_fixture/sel_missing.lox", &exitCode);
    if (exitCode == 0) {
        fail("selective missing: expected error, got ok");
    } else if (strstr(out, "Undefined property") == NULL) {
        fail("selective missing: wrong error '%s'", out);
    } else {
        pass();
    }
    free(out);
}

/* Stage 64.6 — empty brace list is a compile error. */
static void testSelectiveImportEmptyList(void) {
    writeFile("/tmp/clox_mod_fixture/sel_empty.lox",
              "import {} from \"mathutil.lox\";\n");
    int exitCode;
    char* out = runCloxPath("/tmp/clox_mod_fixture/sel_empty.lox", &exitCode);
    if (exitCode != 65) {
        fail("selective empty: expected exit 65, got %d (%s)", exitCode, out);
    } else if (strstr(out, "at least one name") == NULL) {
        fail("selective empty: missing error text, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

/* Stage 64.6 — still top-level only. */
static void testSelectiveImportTopLevelOnly(void) {
    writeFile("/tmp/clox_mod_fixture/sel_nested.lox",
              "fun f() {\n"
              "  import { add } from \"mathutil.lox\";\n"
              "}\n");
    int exitCode;
    char* out = runCloxPath("/tmp/clox_mod_fixture/sel_nested.lox", &exitCode);
    if (exitCode != 65) {
        fail("selective top-level: expected exit 65, got %d (%s)", exitCode, out);
    } else if (strstr(out, "Can only import at top level") == NULL) {
        fail("selective top-level: missing error text, got '%s'", out);
    } else {
        pass();
    }
    free(out);
}

/* Stage 64.6 — cache: selective + whole-module import same path share identity. */
static void testSelectiveImportSharesCache(void) {
    writeFile("/tmp/clox_mod_fixture/sel_cache.lox",
              "import { add } from \"mathutil.lox\";\n"
              "import \"mathutil.lox\" as m;\n"
              "print add == m.add;\n");
    int exitCode;
    char* out = runCloxPath("/tmp/clox_mod_fixture/sel_cache.lox", &exitCode);
    if (exitCode != 0) {
        fail("selective cache: expected exit 0, got %d (%s)", exitCode, out);
    } else if (!outputsEqual(out, "true\n")) {
        fail("selective cache: unexpected output '%s'", out);
    } else {
        pass();
    }
    free(out);
}

/* Stage 64.7 — rename in braces: import { export as bind }. */
static void testSelectiveImportRename(void) {
    system("mkdir -p /tmp/clox_mod_fixture");
    writeFile("/tmp/clox_mod_fixture/mathutil.lox",
              "fun add(a, b) { return a + b; }\n"
              "var PI = 3.14;\n");
    writeFile("/tmp/clox_mod_fixture/sel_rename.lox",
              "import { add as sum, PI } from \"mathutil.lox\";\n"
              "print sum(4, 6);\n"
              "print PI;\n");
    int exitCode;
    char* out = runCloxPath("/tmp/clox_mod_fixture/sel_rename.lox", &exitCode);
    if (exitCode != 0) {
        fail("selective rename: expected exit 0, got %d (%s)", exitCode, out);
    } else if (!outputsEqual(out, "10\n3.14\n")) {
        fail("selective rename: unexpected output '%s'", out);
    } else {
        pass();
    }
    free(out);
}

/* Stage 64.7 — export name is looked up; bind name is local (export not bound). */
static void testSelectiveImportRenameDoesNotBindExport(void) {
    writeFile("/tmp/clox_mod_fixture/sel_rename_only.lox",
              "import { add as sum } from \"mathutil.lox\";\n"
              "print add;\n");
    int exitCode;
    char* out = runCloxPath("/tmp/clox_mod_fixture/sel_rename_only.lox", &exitCode);
    if (exitCode == 0) {
        fail("selective rename unbound export: expected error, got ok (%s)", out);
    } else if (strstr(out, "Undefined") == NULL) {
        fail("selective rename unbound export: wrong error '%s'", out);
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

    /* Stage 64.2: modules import syntax + OP_IMPORT. */
    testImportBasic();
    testImportCacheIdentity();
    testImportTopLevelOnly();

    /* Stage 64.4: cycles + missing file. */
    testImportCycleFunctionsOk();
    testImportCycleLoadingGetError();
    testImportMissingFile();

    /* Stage 64.5: main script is a module. */
    testMainAsModuleCycle();
    testMainAsModuleExport();

    /* Stage 64.6: selective import { a, b } from "path". */
    testSelectiveImportBasic();
    testSelectiveImportDoesNotBindOthers();
    testSelectiveImportMissingExport();
    testSelectiveImportEmptyList();
    testSelectiveImportTopLevelOnly();
    testSelectiveImportSharesCache();

    /* Stage 64.7: rename-in-braces (finish selective surface). */
    testSelectiveImportRename();
    testSelectiveImportRenameDoesNotBindExport();

    printf("%d passed, %d failed\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
