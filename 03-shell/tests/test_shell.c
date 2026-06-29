/*
 * test_shell.c - Test harness for the mira shell
 *
 * Tests tokenizer, parser, builtins, pipes, redirects, and job control.
 */

#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE 1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>

#include "src/shell.h"

/* Provide globals that the shell modules reference (normally in main.c) */
job_t *job_list     = NULL;
int    next_job_id   = 1;
int    last_exit_status = 0;
pid_t  shell_pgid    = 0;
int    shell_terminal = 0;
int    parse_background = 0;
volatile sig_atomic_t sigint_received  = 0;
volatile sig_atomic_t sigtstp_received = 0;

/* ── Test counters ─────────────────────────────────────────── */

static int tests_passed = 0;
static int tests_failed = 0;
static int tests_total  = 0;

#define TEST(name) do { \
    tests_total++; \
    printf("  TEST: %-50s", #name); \
} while (0)

#define PASS() do { \
    printf("PASS\n"); \
    tests_passed++; \
} while (0)

#define FAIL(msg) do { \
    printf("FAIL: %s\n", msg); \
    tests_failed++; \
} while (0)

#define ASSERT_EQ_STR(a, b) do { \
    if ((a) == NULL || (b) == NULL) { \
        if ((a) != (b)) { FAIL("one is NULL"); return; } \
    } else if (strcmp((a), (b)) != 0) { \
        FAIL("string mismatch"); return; \
    } \
} while (0)

#define ASSERT_EQ_INT(a, b) do { \
    if ((a) != (b)) { \
        printf("FAIL: expected %d, got %d\n", (int)(b), (int)(a)); \
        tests_failed++; return; \
    } \
} while (0)

#define ASSERT_TRUE(cond) do { \
    if (!(cond)) { \
        FAIL(#cond " is false"); return; \
    } \
} while (0)

#define ASSERT_NULL(p) do { \
    if ((p) != NULL) { \
        FAIL("expected NULL"); return; \
    } \
} while (0)

#define ASSERT_NOT_NULL(p) do { \
    if ((p) == NULL) { \
        FAIL("expected non-NULL"); return; \
    } \
} while (0)

/* ── Tokenizer tests ───────────────────────────────────────── */

void test_simple_word(void)
{
    TEST(simple_word);
    token_t *tokens = tokenize("hello");
    ASSERT_NOT_NULL(tokens);
    ASSERT_EQ_INT(tokens->type, TOK_WORD);
    ASSERT_EQ_STR(tokens->value, "hello");
    ASSERT_EQ_INT(tokens->next->type, TOK_EOF);
    token_free(tokens);
    PASS();
}

void test_multiple_words(void)
{
    TEST(multiple_words);
    token_t *tokens = tokenize("echo hello world");
    ASSERT_NOT_NULL(tokens);
    ASSERT_EQ_INT(tokens->type, TOK_WORD);
    ASSERT_EQ_STR(tokens->value, "echo");
    ASSERT_EQ_INT(tokens->next->type, TOK_WORD);
    ASSERT_EQ_STR(tokens->next->value, "hello");
    ASSERT_EQ_INT(tokens->next->next->type, TOK_WORD);
    ASSERT_EQ_STR(tokens->next->next->value, "world");
    token_free(tokens);
    PASS();
}

void test_double_quotes(void)
{
    TEST(double_quotes);
    token_t *tokens = tokenize("echo \"hello world\"");
    ASSERT_NOT_NULL(tokens);
    ASSERT_EQ_INT(tokens->type, TOK_WORD);
    ASSERT_EQ_STR(tokens->value, "echo");
    ASSERT_EQ_INT(tokens->next->type, TOK_WORD);
    ASSERT_EQ_STR(tokens->next->value, "hello world");
    token_free(tokens);
    PASS();
}

void test_single_quotes(void)
{
    TEST(single_quotes);
    token_t *tokens = tokenize("echo 'hello world'");
    ASSERT_NOT_NULL(tokens);
    ASSERT_EQ_INT(tokens->type, TOK_WORD);
    ASSERT_EQ_STR(tokens->value, "echo");
    ASSERT_EQ_INT(tokens->next->type, TOK_WORD);
    ASSERT_EQ_STR(tokens->next->value, "hello world");
    token_free(tokens);
    PASS();
}

void test_escape(void)
{
    TEST(escape);
    token_t *tokens = tokenize("echo hello\\ world");
    ASSERT_NOT_NULL(tokens);
    ASSERT_EQ_INT(tokens->type, TOK_WORD);
    ASSERT_EQ_STR(tokens->value, "echo");
    ASSERT_EQ_INT(tokens->next->type, TOK_WORD);
    ASSERT_EQ_STR(tokens->next->value, "hello world");
    token_free(tokens);
    PASS();
}

void test_pipe(void)
{
    TEST(pipe);
    token_t *tokens = tokenize("ls | grep foo");
    ASSERT_NOT_NULL(tokens);
    ASSERT_EQ_INT(tokens->type, TOK_WORD);
    ASSERT_EQ_STR(tokens->value, "ls");
    ASSERT_EQ_INT(tokens->next->type, TOK_PIPE);
    ASSERT_EQ_INT(tokens->next->next->type, TOK_WORD);
    ASSERT_EQ_STR(tokens->next->next->value, "grep");
    token_free(tokens);
    PASS();
}

void test_background(void)
{
    TEST(background);
    token_t *tokens = tokenize("sleep 1 &");
    ASSERT_NOT_NULL(tokens);
    ASSERT_EQ_INT(tokens->type, TOK_WORD);
    ASSERT_EQ_STR(tokens->value, "sleep");
    ASSERT_EQ_INT(tokens->next->type, TOK_WORD);
    ASSERT_EQ_STR(tokens->next->value, "1");
    ASSERT_EQ_INT(tokens->next->next->type, TOK_BG);
    token_free(tokens);
    PASS();
}

void test_redirects(void)
{
    TEST(redirects);
    token_t *tokens = tokenize("cat < in > out 2> err");
    ASSERT_NOT_NULL(tokens);

    /* cat */
    ASSERT_EQ_INT(tokens->type, TOK_WORD);
    ASSERT_EQ_STR(tokens->value, "cat");

    /* < in */
    ASSERT_EQ_INT(tokens->next->type, TOK_REDIR_IN);
    ASSERT_EQ_INT(tokens->next->next->type, TOK_WORD);
    ASSERT_EQ_STR(tokens->next->next->value, "in");

    /* > out */
    ASSERT_EQ_INT(tokens->next->next->next->type, TOK_REDIR_OUT);
    ASSERT_EQ_INT(tokens->next->next->next->next->type, TOK_WORD);
    ASSERT_EQ_STR(tokens->next->next->next->next->value, "out");

    /* 2> err */
    ASSERT_EQ_INT(tokens->next->next->next->next->next->type, TOK_REDIR_ERR);
    ASSERT_EQ_INT(tokens->next->next->next->next->next->next->type, TOK_WORD);
    ASSERT_EQ_STR(tokens->next->next->next->next->next->next->value, "err");

    token_free(tokens);
    PASS();
}

void test_append_redirect(void)
{
    TEST(append_redirect);
    token_t *tokens = tokenize("echo hi >> out");
    ASSERT_NOT_NULL(tokens);
    ASSERT_EQ_INT(tokens->type, TOK_WORD);
    ASSERT_EQ_STR(tokens->value, "echo");
    ASSERT_EQ_INT(tokens->next->type, TOK_WORD);
    ASSERT_EQ_STR(tokens->next->value, "hi");
    ASSERT_EQ_INT(tokens->next->next->type, TOK_REDIR_APPEND);
    ASSERT_EQ_INT(tokens->next->next->next->type, TOK_WORD);
    ASSERT_EQ_STR(tokens->next->next->next->value, "out");
    token_free(tokens);
    PASS();
}

void test_empty_input(void)
{
    TEST(empty_input);
    token_t *tokens = tokenize("");
    ASSERT_NOT_NULL(tokens);
    ASSERT_EQ_INT(tokens->type, TOK_EOF);
    token_free(tokens);
    PASS();
}

/* ── Parser tests ───────────────────────────────────────────── */

void test_parse_simple_command(void)
{
    TEST(parse_simple_command);
    token_t *tokens = tokenize("ls -la");
    parse_background = 0;
    command_t *cmd = parse(tokens);
    ASSERT_NOT_NULL(cmd);
    ASSERT_EQ_STR(cmd->argv[0], "ls");
    ASSERT_EQ_STR(cmd->argv[1], "-la");
    ASSERT_NULL(cmd->next);
    ASSERT_NULL(cmd->infile);
    ASSERT_NULL(cmd->outfile);
    ASSERT_NULL(cmd->errfile);
    token_free(tokens);
    command_free(cmd);
    PASS();
}

void test_parse_pipeline(void)
{
    TEST(parse_pipeline);
    token_t *tokens = tokenize("ls | grep foo | wc -l");
    parse_background = 0;
    command_t *cmd = parse(tokens);
    ASSERT_NOT_NULL(cmd);
    ASSERT_EQ_STR(cmd->argv[0], "ls");
    ASSERT_NOT_NULL(cmd->next);
    ASSERT_EQ_STR(cmd->next->argv[0], "grep");
    ASSERT_EQ_STR(cmd->next->argv[1], "foo");
    ASSERT_NOT_NULL(cmd->next->next);
    ASSERT_EQ_STR(cmd->next->next->argv[0], "wc");
    ASSERT_EQ_STR(cmd->next->next->argv[1], "-l");
    ASSERT_NULL(cmd->next->next->next);
    token_free(tokens);
    command_free(cmd);
    PASS();
}

void test_parse_redirects(void)
{
    TEST(parse_redirects);
    token_t *tokens = tokenize("cat < input.txt > output.txt");
    parse_background = 0;
    command_t *cmd = parse(tokens);
    ASSERT_NOT_NULL(cmd);
    ASSERT_EQ_STR(cmd->argv[0], "cat");
    ASSERT_EQ_STR(cmd->infile, "input.txt");
    ASSERT_EQ_STR(cmd->outfile, "output.txt");
    ASSERT_EQ_INT(cmd->append, 0);
    token_free(tokens);
    command_free(cmd);
    PASS();
}

void test_parse_background(void)
{
    TEST(parse_background);
    token_t *tokens = tokenize("sleep 10 &");
    parse_background = 0;
    command_t *cmd = parse(tokens);
    ASSERT_NOT_NULL(cmd);
    ASSERT_EQ_STR(cmd->argv[0], "sleep");
    ASSERT_EQ_INT(parse_background, 1);
    token_free(tokens);
    command_free(cmd);
    PASS();
}

void test_parse_stderr_redirect(void)
{
    TEST(parse_stderr_redirect);
    token_t *tokens = tokenize("ls 2>/dev/null");
    parse_background = 0;
    command_t *cmd = parse(tokens);
    ASSERT_NOT_NULL(cmd);
    ASSERT_EQ_STR(cmd->argv[0], "ls");
    ASSERT_EQ_STR(cmd->errfile, "/dev/null");
    token_free(tokens);
    command_free(cmd);
    PASS();
}

void test_parse_empty(void)
{
    TEST(parse_empty);
    token_t *tokens = tokenize("");
    parse_background = 0;
    command_t *cmd = parse(tokens);
    ASSERT_NULL(cmd);
    token_free(tokens);
    PASS();
}

/* ── Built-in command tests ─────────────────────────────────── */

void test_builtin_cd(void)
{
    TEST(builtin_cd);
    char orig[4096];
    getcwd(orig, sizeof(orig));

    command_t cmd = {0};
    char *args[] = {"cd", "/tmp", NULL};
    cmd.argv = args;

    int ret = builtin_cd(&cmd);
    ASSERT_EQ_INT(ret, 0);

    char cwd[4096];
    getcwd(cwd, sizeof(cwd));
    ASSERT_EQ_STR(cwd, "/tmp");

    /* Restore */
    chdir(orig);
    PASS();
}

void test_builtin_pwd(void)
{
    TEST(builtin_pwd);

    command_t cmd = {0};
    char *args[] = {"pwd", NULL};
    cmd.argv = args;

    int ret = builtin_pwd(&cmd);
    ASSERT_EQ_INT(ret, 0);
    PASS();
}

void test_builtin_echo(void)
{
    TEST(builtin_echo);
    command_t cmd = {0};
    char *args[] = {"echo", "hello", "world", NULL};
    cmd.argv = args;

    int ret = builtin_echo(&cmd);
    ASSERT_EQ_INT(ret, 0);
    PASS();
}

void test_builtin_export(void)
{
    TEST(builtin_export);
    command_t cmd = {0};
    char arg[] = "MIRA_TEST_VAR=hello";
    char *args[] = {"export", arg, NULL};
    cmd.argv = args;

    int ret = builtin_export(&cmd);
    ASSERT_EQ_INT(ret, 0);
    ASSERT_EQ_STR(getenv("MIRA_TEST_VAR"), "hello");

    /* Clean up */
    unsetenv("MIRA_TEST_VAR");
    PASS();
}

void test_builtin_unset(void)
{
    TEST(builtin_unset);
    setenv("MIRA_TEST_VAR2", "test", 1);
    ASSERT_EQ_STR(getenv("MIRA_TEST_VAR2"), "test");

    command_t cmd = {0};
    char *args[] = {"unset", "MIRA_TEST_VAR2", NULL};
    cmd.argv = args;

    int ret = builtin_unset(&cmd);
    ASSERT_EQ_INT(ret, 0);
    ASSERT_NULL(getenv("MIRA_TEST_VAR2"));
    PASS();
}

/* ── Integration tests (fork/exec) ─────────────────────────── */

void test_external_command(void)
{
    TEST(external_command);
    /* Test running an external command through the shell */
    int pipefd[2];
    pipe(pipefd);

    fflush(stdout);

    pid_t pid = fork();
    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);

        /* Build a simple command and execute it */
        shell_terminal = 0;
        parse_background = 0;

        token_t *tokens = tokenize("/bin/echo integration_test");
        command_t *cmd = parse(tokens);

        int ret = execute(cmd);
        fflush(stdout);
        command_free(cmd);
        token_free(tokens);
        _exit(ret);
    }

    close(pipefd[1]);
    char buf[256] = {0};
    read(pipefd[0], buf, sizeof(buf) - 1);
    close(pipefd[0]);

    int status;
    waitpid(pid, &status, 0);

    ASSERT_TRUE(strstr(buf, "integration_test") != NULL);
    PASS();
}

void test_pipe_command(void)
{
    TEST(pipe_command);
    /* Use a temp file to avoid pipe buffer inheritance issues */
    const char *tmpfile = "/tmp/mira_shell_pipe_test.txt";
    unlink(tmpfile);

    fflush(stdout);

    pid_t pid = fork();
    if (pid == 0) {
        /* Child: redirect stdout to temp file */
        int fd = open(tmpfile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) _exit(1);
        dup2(fd, STDOUT_FILENO);
        close(fd);

        shell_terminal = 0;
        parse_background = 0;

        token_t *tokens = tokenize("echo pipe_test | cat");
        command_t *cmd = parse(tokens);

        int ret = execute(cmd);
        fflush(stdout);
        command_free(cmd);
        token_free(tokens);
        _exit(ret);
    }

    int status;
    waitpid(pid, &status, 0);

    FILE *f = fopen(tmpfile, "r");
    ASSERT_NOT_NULL(f);
    char buf[256] = {0};
    fgets(buf, sizeof(buf), f);
    fclose(f);
    unlink(tmpfile);

    ASSERT_TRUE(strstr(buf, "pipe_test") != NULL);
    PASS();
}

void test_output_redirect(void)
{
    TEST(output_redirect);
    const char *tmpfile = "/tmp/mira_shell_test_redirect.txt";
    unlink(tmpfile);

    fflush(stdout);

    pid_t pid = fork();
    if (pid == 0) {
        shell_terminal = 0;
        parse_background = 0;

        char cmd_str[256];
        snprintf(cmd_str, sizeof(cmd_str), "echo redirect_test > %s", tmpfile);
        token_t *tokens = tokenize(cmd_str);
        command_t *cmd = parse(tokens);
        execute(cmd);
        command_free(cmd);
        token_free(tokens);
        _exit(0);
    }

    int status;
    waitpid(pid, &status, 0);

    FILE *f = fopen(tmpfile, "r");
    ASSERT_NOT_NULL(f);
    char buf[256] = {0};
    fgets(buf, sizeof(buf), f);
    fclose(f);
    unlink(tmpfile);

    ASSERT_TRUE(strstr(buf, "redirect_test") != NULL);
    PASS();
}

/* ── PATH lookup tests ──────────────────────────────────────── */

void test_find_in_path(void)
{
    TEST(find_in_path);
    char *path = find_in_path("ls");
    ASSERT_NOT_NULL(path);
    /* Should find ls in /usr/bin/ls or /bin/ls */
    ASSERT_TRUE(strstr(path, "ls") != NULL);
    free(path);

    path = find_in_path("nonexistent_command_xyz");
    ASSERT_NULL(path);

    path = find_in_path("/bin/ls");
    ASSERT_NOT_NULL(path);
    free(path);
    PASS();
}

/* ── Main test runner ───────────────────────────────────────── */

int main(void)
{
    printf("=== Mira Shell Tests ===\n\n");

    printf("--- Tokenizer ---\n");
    test_simple_word();
    test_multiple_words();
    test_double_quotes();
    test_single_quotes();
    test_escape();
    test_pipe();
    test_background();
    test_redirects();
    test_append_redirect();
    test_empty_input();

    printf("\n--- Parser ---\n");
    test_parse_simple_command();
    test_parse_pipeline();
    test_parse_redirects();
    test_parse_background();
    test_parse_stderr_redirect();
    test_parse_empty();

    printf("\n--- Built-ins ---\n");
    test_builtin_cd();
    test_builtin_pwd();
    test_builtin_echo();
    test_builtin_export();
    test_builtin_unset();

    printf("\n--- PATH Lookup ---\n");
    test_find_in_path();

    printf("\n--- Integration ---\n");
    test_external_command();
    test_pipe_command();
    test_output_redirect();

    printf("\n=== Results: %d/%d passed, %d failed ===\n",
           tests_passed, tests_total, tests_failed);

    return tests_failed > 0 ? 1 : 0;
}