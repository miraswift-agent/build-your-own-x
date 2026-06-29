/*
 * execute.c - Command execution engine
 *
 * Handles:
 *   - PATH lookup for executables
 *   - Fork/exec for external commands
 *   - Pipeline execution (multiple commands connected by pipes)
 *   - I/O redirection (>, >>, <, 2>)
 *   - Background execution (&)
 *   - Built-in command dispatch
 */

#include "shell.h"

/* ── Global: set by parse() when & is found ─────────────────── */
extern int parse_background;

/* ── Check if a command name is a builtin ───────────────────── */

static int is_builtin(const char *name)
{
    if (!name) return 0;
    return (strcmp(name, "cd") == 0     ||
            strcmp(name, "exit") == 0   ||
            strcmp(name, "pwd") == 0    ||
            strcmp(name, "echo") == 0   ||
            strcmp(name, "export") == 0  ||
            strcmp(name, "unset") == 0  ||
            strcmp(name, "jobs") == 0   ||
            strcmp(name, "fg") == 0     ||
            strcmp(name, "bg") == 0);
}

/* ── PATH lookup ───────────────────────────────────────────── */

char *find_in_path(const char *name)
{
    /* If name contains '/', treat as absolute/relative path */
    if (strchr(name, '/')) {
        if (access(name, X_OK) == 0)
            return strdup(name);
        return NULL;
    }

    const char *path_env = getenv("PATH");
    if (!path_env) path_env = "/usr/bin:/bin";

    char *path_copy = strdup(path_env);
    if (!path_copy) return NULL;

    char *dir = strtok(path_copy, ":");
    while (dir) {
        char full[4096];
        snprintf(full, sizeof(full), "%s/%s", dir, name);
        if (access(full, X_OK) == 0) {
            free(path_copy);
            return strdup(full);
        }
        dir = strtok(NULL, ":");
    }

    free(path_copy);
    return NULL;
}

/* ── Built-in command dispatch ──────────────────────────────── */

int exec_builtin(command_t *cmd)
{
    if (!cmd->argv || !cmd->argv[0])
        return 0;

    const char *name = cmd->argv[0];

    if (strcmp(name, "cd") == 0)     return builtin_cd(cmd);
    if (strcmp(name, "exit") == 0)   return builtin_exit(cmd);
    if (strcmp(name, "pwd") == 0)    return builtin_pwd(cmd);
    if (strcmp(name, "echo") == 0)   return builtin_echo(cmd);
    if (strcmp(name, "export") == 0) return builtin_export(cmd);
    if (strcmp(name, "unset") == 0)  return builtin_unset(cmd);
    if (strcmp(name, "jobs") == 0) {
        update_jobs();
        print_jobs();
        free_done_jobs();
        return 0;
    }
    if (strcmp(name, "fg") == 0) {
        int jid = 1;
        if (cmd->argv[1]) {
            const char *arg = cmd->argv[1];
            if (arg[0] == '%') arg++;
            jid = atoi(arg);
        }
        return fg_job(jid);
    }
    if (strcmp(name, "bg") == 0) {
        int jid = 1;
        if (cmd->argv[1]) {
            const char *arg = cmd->argv[1];
            if (arg[0] == '%') arg++;
            jid = atoi(arg);
        }
        return bg_job(jid);
    }

    return -1;  /* Not a built-in */
}

/* ── Set up redirections for a command ─────────────────────── */

static int setup_redirections(command_t *cmd)
{
    /* Input redirect */
    if (cmd->infile) {
        int fd = open(cmd->infile, O_RDONLY);
        if (fd < 0) {
            fprintf(stderr, "mira: %s: %s\n", cmd->infile, strerror(errno));
            return -1;
        }
        dup2(fd, STDIN_FILENO);
        close(fd);
    }

    /* Output redirect */
    if (cmd->outfile) {
        int flags = O_WRONLY | O_CREAT;
        flags |= cmd->append ? O_APPEND : O_TRUNC;
        int fd = open(cmd->outfile, flags, 0644);
        if (fd < 0) {
            fprintf(stderr, "mira: %s: %s\n", cmd->outfile, strerror(errno));
            return -1;
        }
        dup2(fd, STDOUT_FILENO);
        close(fd);
    }

    /* Stderr redirect */
    if (cmd->errfile) {
        int fd = open(cmd->errfile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) {
            fprintf(stderr, "mira: %s: %s\n", cmd->errfile, strerror(errno));
            return -1;
        }
        dup2(fd, STDERR_FILENO);
        close(fd);
    }

    return 0;
}

/* ── Save and restore file descriptors ─────────────────────── */

static int saved_stdout_fd = -1;
static int saved_stdin_fd  = -1;
static int saved_stderr_fd = -1;

static void save_fds(command_t *cmd)
{
    if (cmd->outfile) {
        saved_stdout_fd = dup(STDOUT_FILENO);
    }
    if (cmd->infile) {
        saved_stdin_fd = dup(STDIN_FILENO);
    }
    if (cmd->errfile) {
        saved_stderr_fd = dup(STDERR_FILENO);
    }
}

static void restore_fds(command_t *cmd)
{
    (void)cmd;
    if (saved_stdout_fd >= 0) {
        dup2(saved_stdout_fd, STDOUT_FILENO);
        close(saved_stdout_fd);
        saved_stdout_fd = -1;
    }
    if (saved_stdin_fd >= 0) {
        dup2(saved_stdin_fd, STDIN_FILENO);
        close(saved_stdin_fd);
        saved_stdin_fd = -1;
    }
    if (saved_stderr_fd >= 0) {
        dup2(saved_stderr_fd, STDERR_FILENO);
        close(saved_stderr_fd);
        saved_stderr_fd = -1;
    }
}

/* ── Count pipeline stages ──────────────────────────────────── */

static int pipeline_length(command_t *cmd)
{
    int n = 0;
    for (; cmd; cmd = cmd->next) n++;
    return n;
}

/* ── Execute a single builtin in the parent process ────────── */
/* Handles redirections by saving/restoring fds around the call */

static int execute_builtin_with_redirs(command_t *cmd)
{
    int ret;

    /* Flush stdout/stderr before changing fds */
    fflush(stdout);
    fflush(stderr);

    /* Save current fds if redirections are needed */
    save_fds(cmd);

    /* Set up redirections (overwrites fds) */
    if (setup_redirections(cmd) < 0) {
        restore_fds(cmd);
        return 1;
    }

    /* Run the builtin */
    ret = exec_builtin(cmd);

    /* Flush before restoring */
    fflush(stdout);
    fflush(stderr);

    /* Restore original fds */
    restore_fds(cmd);
    fflush(stdout);
    fflush(stderr);

    return ret;
}

/* ── Execute a pipeline ────────────────────────────────────── */

int execute(command_t *cmd)
{
    if (!cmd || !cmd->argv)
        return 0;

    /* Single command, no pipe: check for builtin */
    if (!cmd->next && cmd->argv && cmd->argv[0] && is_builtin(cmd->argv[0])) {
        /* Builtins with redirections: save/restore fds around the call */
        return execute_builtin_with_redirs(cmd);
    }

    int      ncmds    = pipeline_length(cmd);
    int      bg       = parse_background;
    pid_t   *pids     = malloc(ncmds * sizeof(pid_t));
    int     *pipes    = NULL;
    int      pipe_cnt = (ncmds > 1) ? (ncmds - 1) * 2 : 0;

    if (!pids) { perror("malloc"); return 1; }

    /* Create pipes if needed */
    if (pipe_cnt > 0) {
        pipes = malloc(pipe_cnt * sizeof(int));
        if (!pipes) { perror("malloc"); free(pids); return 1; }
        for (int i = 0; i < ncmds - 1; i++) {
            if (pipe(pipes + i * 2) < 0) {
                perror("pipe");
                /* Close any pipes already created */
                for (int j = 0; j < i * 2; j++)
                    close(pipes[j]);
                free(pids);
                free(pipes);
                return 1;
            }
        }
    }

    /* Create process group ID from first child */
    pid_t first_pgid = 0;

    for (int i = 0; i < ncmds; i++) {
        command_t *c = cmd;
        /* Walk to the i-th command */
        for (int skip = 0; skip < i; skip++) c = c->next;

        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            /* Clean up pipes */
            for (int j = 0; j < pipe_cnt; j++)
                close(pipes[j]);
            free(pids);
            free(pipes);
            return 1;
        }

        if (pid == 0) {
            /* ── CHILD PROCESS ──────────────────────────────── */

            /* Restore default signal handling */
            restore_signals();

            /* Set up pipe connections */
            if (pipes) {
                /* Connect stdin from previous pipe */
                if (i > 0) {
                    dup2(pipes[(i - 1) * 2], STDIN_FILENO);
                }
                /* Connect stdout to next pipe */
                if (i < ncmds - 1) {
                    dup2(pipes[i * 2 + 1], STDOUT_FILENO);
                }
                /* Close all pipe fds */
                for (int j = 0; j < pipe_cnt; j++)
                    close(pipes[j]);
            }

            /* Set up I/O redirections */
            if (setup_redirections(c) < 0)
                _exit(1);

            /* Set process group (for job control) */
            if (first_pgid == 0)
                setpgid(0, 0);  /* New group, become leader */
            else
                setpgid(0, first_pgid);

            /* Check if this pipeline stage is a builtin */
            if (is_builtin(c->argv[0])) {
                int bret = exec_builtin(c);
                fflush(stdout);
                fflush(stderr);
                _exit(bret);
            }

            /* Find and exec the command */
            char *path = find_in_path(c->argv[0]);
            if (!path) {
                fprintf(stderr, "mira: %s: command not found\n", c->argv[0]);
                _exit(127);
            }
            execv(path, c->argv);
            perror(path);
            free(path);
            _exit(126);
        }

        /* ── PARENT ─────────────────────────────────────────── */
        pids[i] = pid;

        /* Set up process group for job control */
        if (i == 0) {
            first_pgid = pid;
            setpgid(pid, pid);  /* New process group */

            /* Give terminal to the job if foreground */
            if (!bg && shell_terminal) {
                tcsetpgrp(STDIN_FILENO, pid);
            }
        } else {
            setpgid(pid, first_pgid);  /* Same group as first */
        }
    }

    /* Close all pipe fds in parent */
    for (int j = 0; j < pipe_cnt; j++)
        close(pipes[j]);

    /* Build command string for job table */
    char cmd_str[1024] = {0};
    {
        command_t *c = cmd;
        int off = 0;
        while (c && off < (int)sizeof(cmd_str) - 1) {
            for (int a = 0; c->argv[a] && off < (int)sizeof(cmd_str) - 1; a++) {
                int n = snprintf(cmd_str + off, sizeof(cmd_str) - off,
                                 "%s%s", (a ? " " : ""), c->argv[a]);
                off += n;
            }
            if (c->next && off < (int)sizeof(cmd_str) - 2) {
                cmd_str[off++] = ' ';
                cmd_str[off++] = '|';
                cmd_str[off++] = ' ';
            }
            c = c->next;
        }
        if (bg && off < (int)sizeof(cmd_str) - 2) {
            cmd_str[off++] = ' ';
            cmd_str[off++] = '&';
        }
    }

    if (bg) {
        /* Background job: add to job table, don't wait */
        job_t *j = add_job(first_pgid, cmd_str, JOB_RUNNING);
        fprintf(stderr, "[%d] %d\n", j->job_id, first_pgid);
        free(pids);
        free(pipes);
        return 0;
    }

    /* Foreground: wait for all processes in the pipeline */
    int status = 0;
    for (int i = 0; i < ncmds; i++) {
        int wstatus;
        pid_t wpid;

        do {
            wpid = waitpid(pids[i], &wstatus, WUNTRACED);
        } while (wpid == -1 && errno == EINTR);

        if (wpid < 0) {
            if (errno != ECHILD)
                perror("waitpid");
            continue;
        }

        if (WIFSTOPPED(wstatus)) {
            /* Job was stopped (Ctrl+Z) — add to job table */
            add_job(first_pgid, cmd_str, JOB_STOPPED);
            fprintf(stderr, "\n[%d]+  Stopped    %s\n",
                    next_job_id - 1, cmd_str);
            status = 0;
            break;   /* Don't wait for remaining procs */
        }

        if (WIFEXITED(wstatus)) {
            status = WEXITSTATUS(wstatus);
        } else if (WIFSIGNALED(wstatus)) {
            status = 128 + WTERMSIG(wstatus);
        }
    }

    /* Shell regains terminal control */
    if (shell_terminal) {
        tcsetpgrp(STDIN_FILENO, shell_pgid);
    }

    free(pids);
    free(pipes);

    last_exit_status = status;
    return status;
}