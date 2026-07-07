/*
 * Lantern VM — Stage 3 compiler tests.
 */

#include "compiler.h"
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define FAIL(message) do { fprintf(stderr, "FAIL: %s\n", message); exit(1); } while (0)

static Value run_string(const char *source, LVMError *err_out) {
    Compiler c;
    compiler_init(&c, source);
    if (!compiler_compile(&c)) {
        fprintf(stderr, "Compile error: %s\n", compiler_error_string(&c));
        compiler_free(&c);
        *err_out = LVM_ERR_RUNTIME;
        return NULL_VAL;
    }

    Chunk *main_chunk = compiler_get_main(&c);
    if (!main_chunk) {
        compiler_free(&c);
        *err_out = LVM_ERR_RUNTIME;
        return NULL_VAL;
    }

    LanternVM *vm = lvm_new();
    for (int i = 0; i < c.assembler.chunk_count; i++) {
        lvm_add_function(vm, &c.assembler.chunks[i]);
    }

    *err_out = lvm_run(vm);
    /* The VM's OP_RETURN for main consumes the return value but leaves it in
     * memory at stack[local_count]. */
    Value result = *err_out == LVM_OK ? vm->stack[main_chunk->local_count] : NULL_VAL;
    lvm_free(vm);
    compiler_free(&c);
    return result;
}

typedef struct {
    int saved_stdout;
    int pipe_fd[2];
} Capture;

static void capture_start(Capture *cap) {
    fflush(stdout); /* flush any buffered output before redirecting */
    if (pipe(cap->pipe_fd) != 0) FAIL("pipe failed");
    cap->saved_stdout = dup(STDOUT_FILENO);
    if (cap->saved_stdout < 0) FAIL("dup failed");
    if (dup2(cap->pipe_fd[1], STDOUT_FILENO) < 0) FAIL("dup2 failed");
    close(cap->pipe_fd[1]);
}

static void capture_stop(Capture *cap, char *buf, size_t bufsize) {
    fflush(stdout);
    if (dup2(cap->saved_stdout, STDOUT_FILENO) < 0) FAIL("dup2 restore failed");
    close(cap->saved_stdout);

    ssize_t n = read(cap->pipe_fd[0], buf, bufsize - 1);
    if (n < 0) n = 0;
    buf[n] = '\0';
    close(cap->pipe_fd[0]);
}

static void test_arithmetic(void) {
    const char *src = "func main() { return (3 + 4) * 2 - 5 % 3; }";
    LVMError err;
    Value v = run_string(src, &err);
    assert(err == LVM_OK);
    assert(v.type == VAL_INT);
    /* With standard precedence: 5 % 3 = 2, (3+4)*2 = 14, 14 - 2 = 12. */
    assert(v.as.integer == 12);
    printf("PASS: arithmetic -> %" PRId64 "\n", v.as.integer);
}

static void test_if_else(void) {
    const char *src =
        "func main() {\n"
        "    var x = 10;\n"
        "    if (x > 5) { return 1; } else { return 0; }\n"
        "}\n";
    LVMError err;
    Value v = run_string(src, &err);
    assert(err == LVM_OK);
    assert(v.type == VAL_INT);
    assert(v.as.integer == 1);
    printf("PASS: if/else -> %" PRId64 "\n", v.as.integer);
}

static void test_while(void) {
    const char *src =
        "func main() {\n"
        "    var i = 0;\n"
        "    var sum = 0;\n"
        "    while (i < 10) {\n"
        "        sum = sum + i;\n"
        "        i = i + 1;\n"
        "    }\n"
        "    return sum;\n"
        "}\n";
    LVMError err;
    Value v = run_string(src, &err);
    assert(err == LVM_OK);
    assert(v.type == VAL_INT);
    assert(v.as.integer == 45);
    printf("PASS: while -> %" PRId64 "\n", v.as.integer);
}

static void test_function_call(void) {
    const char *src =
        "func add(a, b) { return a + b; }\n"
        "func main() { return add(3, 4); }\n";
    LVMError err;
    Value v = run_string(src, &err);
    assert(err == LVM_OK);
    assert(v.type == VAL_INT);
    assert(v.as.integer == 7);
    printf("PASS: function call -> %" PRId64 "\n", v.as.integer);
}

static void test_recursion(void) {
    const char *src =
        "func fib(n) {\n"
        "    if (n <= 1) { return n; }\n"
        "    return fib(n - 1) + fib(n - 2);\n"
        "}\n"
        "func main() { return fib(10); }\n";
    LVMError err;
    Value v = run_string(src, &err);
    assert(err == LVM_OK);
    assert(v.type == VAL_INT);
    assert(v.as.integer == 55);
    printf("PASS: recursion -> %" PRId64 "\n", v.as.integer);
}

static void test_print_builtin(void) {
    const char *src =
        "func main() {\n"
        "    println(42);\n"
        "    return 0;\n"
        "}\n";

    Compiler c;
    compiler_init(&c, src);
    if (!compiler_compile(&c)) {
        fprintf(stderr, "Compile error: %s\n", compiler_error_string(&c));
        compiler_free(&c);
        FAIL("print builtin compile failed");
    }

    LanternVM *vm = lvm_new();
    for (int i = 0; i < c.assembler.chunk_count; i++) {
        lvm_add_function(vm, &c.assembler.chunks[i]);
    }

    char buf[64] = {0};
    Capture cap;
    capture_start(&cap);
    LVMError err = lvm_run(vm);
    capture_stop(&cap, buf, sizeof(buf));

    assert(err == LVM_OK);
    assert(strcmp(buf, "42\n") == 0);
    printf("PASS: print builtin -> %s", buf);

    lvm_free(vm);
    compiler_free(&c);
}

static void test_syntax_error(void) {
    Compiler c;
    compiler_init(&c, "func main() { return }");
    bool ok = compiler_compile(&c);
    assert(!ok);
    printf("PASS: syntax error -> %s\n", compiler_error_string(&c));
    compiler_free(&c);
}

static void test_runtime_error(void) {
    const char *src = "func main() { return 1 + true; }";
    LVMError err;
    Value v = run_string(src, &err);
    (void)v;
    assert(err != LVM_OK);
    printf("PASS: runtime error -> %s\n", lvm_error_string(err));
}

int main(void) {
    test_arithmetic();
    test_if_else();
    test_while();
    test_function_call();
    test_recursion();
    test_print_builtin();
    test_syntax_error();
    test_runtime_error();
    printf("\nAll Stage 3 compiler tests passed.\n");
    return 0;
}
