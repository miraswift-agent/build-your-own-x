/*
 * main.c - Shell entry point and REPL
 *
 * The main read-eval-print loop. Handles:
 *   - Shell initialization (job control, signals, terminal setup)
 *   - Reading input lines
 *   - Tokenizing, parsing, executing
 *   - Checking background jobs between commands
 *   - Graceful shutdown
 */

#include "shell.h"

/* ── Global state ───────────────────────────────────────────── */

pid_t shell_pgid;
int   shell_terminal = 0;
int   parse_background = 0;   /* Set by parse() if & found */

/* Globals referenced from other modules via shell.h externs */
job_t *job_list     = NULL;
int   next_job_id   = 1;
int   last_exit_status = 0;
volatile sig_atomic_t sigint_received  = 0;
volatile sig_atomic_t sigtstp_received = 0;

/* ── Read a line from stdin ─────────────────────────────────── */

static char *read_line(void)
{
    char  *line    = NULL;
    size_t bufsize = 0;
    ssize_t len;

    len = getline(&line, &bufsize, stdin);

    if (len == -1) {
        /* EOF (Ctrl+D) */
        if (feof(stdin)) {
            printf("\n");
            free(line);
            return NULL;
        }
        perror("getline");
        free(line);
        return NULL;
    }

    /* Strip trailing newline */
    if (len > 0 && line[len - 1] == '\n')
        line[len - 1] = '\0';

    return line;
}

/* ── Print the prompt ───────────────────────────────────────── */

static void print_prompt(void)
{
    /* Show current directory in prompt */
    char cwd[256];
    const char *dir = "?";
    if (getcwd(cwd, sizeof(cwd))) {
        /* Show just the last component */
        const char *slash = strrchr(cwd, '/');
        dir = slash ? slash + 1 : cwd;
    }
    printf("mira:%s$ ", dir);
    fflush(stdout);
}

/* ── Main REPL loop ─────────────────────────────────────────── */

void shell_loop(void)
{
    char *line;

    while (1) {
        print_prompt();

        line = read_line();
        if (!line) break;  /* EOF */

        /* Skip blank lines */
        if (line[0] == '\0') {
            free(line);
            continue;
        }

        /* Tokenize */
        token_t *tokens = tokenize(line);
        if (!tokens || tokens->type == TOK_EOF) {
            token_free(tokens);
            free(line);
            continue;
        }

        /* Parse */
        parse_background = 0;  /* Reset before each parse */
        command_t *cmd = parse(tokens);
        token_free(tokens);

        if (!cmd) {
            free(line);
            continue;
        }

        /* Check for SIGINT from previous command */
        if (sigint_received) {
            sigint_received = 0;
            /* Just clear the flag; the user hit Ctrl+C on a blank line */
        }

        /* Execute */
        execute(cmd);

        /* Clean up completed background jobs */
        update_jobs();
        free_done_jobs();

        command_free(cmd);
        free(line);
    }
}

/* ── Entry point ────────────────────────────────────────────── */

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    /* Check if we're running interactively */
    shell_terminal = isatty(STDIN_FILENO);

    if (shell_terminal) {
        /* Put shell in its own process group */
        shell_pgid = getpid();
        if (setpgid(shell_pgid, shell_pgid) < 0) {
            perror("setpgid");
            exit(1);
        }

        /* Take control of the terminal */
        tcsetpgrp(STDIN_FILENO, shell_pgid);
    } else {
        /* Non-interactive mode: use shell's existing pgid */
        shell_pgid = getpgrp();
    }

    /* Install signal handlers */
    setup_signals();

    /* Run the REPL */
    shell_loop();

    /* Clean exit */
    return last_exit_status;
}