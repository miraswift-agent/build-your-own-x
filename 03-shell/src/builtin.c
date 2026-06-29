/*
 * builtin.c - Built-in shell commands
 *
 * Commands that must run in the shell process itself
 * (because they modify shell state: cwd, env, job table).
 */

#include "shell.h"

/* ── cd: change directory ──────────────────────────────────── */

int builtin_cd(command_t *cmd)
{
    const char *dir;

    if (!cmd->argv[1]) {
        /* No argument: go to $HOME */
        dir = getenv("HOME");
        if (!dir) {
            fprintf(stderr, "mira: cd: HOME not set\n");
            return 1;
        }
    } else if (cmd->argv[2]) {
        fprintf(stderr, "mira: cd: too many arguments\n");
        return 1;
    } else {
        dir = cmd->argv[1];
    }

    if (chdir(dir) != 0) {
        fprintf(stderr, "mira: cd: %s: %s\n", dir, strerror(errno));
        return 1;
    }

    /* Update PWD environment variable */
    char cwd[4096];
    if (getcwd(cwd, sizeof(cwd)))
        setenv("PWD", cwd, 1);

    return 0;
}

/* ── exit: leave the shell ──────────────────────────────────── */

int builtin_exit(command_t *cmd)
{
    int status = 0;
    if (cmd->argv[1]) {
        status = atoi(cmd->argv[1]);
    }
    exit(status);
    return 0;  /* unreachable */
}

/* ── pwd: print working directory ───────────────────────────── */

int builtin_pwd(command_t *cmd)
{
    (void)cmd;
    char cwd[4096];
    if (getcwd(cwd, sizeof(cwd))) {
        printf("%s\n", cwd);
        return 0;
    }
    fprintf(stderr, "mira: pwd: %s\n", strerror(errno));
    return 1;
}

/* ── echo: print arguments ──────────────────────────────────── */

int builtin_echo(command_t *cmd)
{
    for (int i = 1; cmd->argv[i]; i++) {
        if (i > 1) putchar(' ');
        fputs(cmd->argv[i], stdout);
    }
    putchar('\n');
    return 0;
}

/* ── export: set environment variable ─────────────────────── */

int builtin_export(command_t *cmd)
{
    if (!cmd->argv[1]) {
        /* No args: print all exported variables */
        extern char **environ;
        for (char **env = environ; *env; env++)
            printf("export %s\n", *env);
        return 0;
    }

    for (int i = 1; cmd->argv[i]; i++) {
        char *name = cmd->argv[i];
        char *eq   = strchr(name, '=');
        if (eq) {
            /* Split at '=': set name=value */
            *eq = '\0';
            const char *var_name = name;
            const char *var_val  = eq + 1;
            if (setenv(var_name, var_val, 1) != 0) {
                fprintf(stderr, "mira: export: %s: %s\n", var_name, strerror(errno));
                return 1;
            }
        } else {
            /* Just a name: mark for export (might not be set yet) */
            /* In a real shell, this would add to export list.
             * For simplicity, we just skip names without values. */
        }
    }
    return 0;
}

/* ── unset: remove environment variable ─────────────────────── */

int builtin_unset(command_t *cmd)
{
    for (int i = 1; cmd->argv[i]; i++) {
        unsetenv(cmd->argv[i]);
    }
    return 0;
}