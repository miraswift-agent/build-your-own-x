/*
 * shell.h - Simple Unix shell
 *
 * A from-scratch implementation of core shell features:
 * REPL, tokenization, parsing, command execution, pipes,
 * I/O redirection, job control, and signal handling.
 */

#ifndef SHELL_H
#define SHELL_H

/* Enable POSIX features (sigaction, strdup, setenv, getline, etc.) */
#if !defined(_POSIX_C_SOURCE) || _POSIX_C_SOURCE < 200809L
#undef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#if !defined(_DEFAULT_SOURCE)
#define _DEFAULT_SOURCE 1
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <ctype.h>

/* ── Job states ────────────────────────────────────────────── */

typedef enum {
    JOB_RUNNING,
    JOB_STOPPED,
    JOB_DONE
} job_state_t;

typedef struct job {
    int          job_id;      /* Sequential job number            */
    pid_t        pgid;        /* Process group ID                 */
    char        *command;     /* Original command line            */
    job_state_t  state;       /* Running, stopped, done           */
    int          notified;    /* Have we reported this job's status? */
    struct job  *next;        /* Linked list                      */
} job_t;

/* ── Token types ───────────────────────────────────────────── */

typedef enum {
    TOK_WORD,         /* Regular word / argument          */
    TOK_PIPE,         /* |                                */
    TOK_BG,           /* &                                */
    TOK_REDIR_OUT,    /* >                                */
    TOK_REDIR_APPEND, /* >>                               */
    TOK_REDIR_IN,     /* <                                */
    TOK_REDIR_ERR,    /* 2>                               */
    TOK_EOF           /* End of input                     */
} token_type_t;

typedef struct token {
    token_type_t  type;
    char         *value;      /* For TOK_WORD: the word           */
    struct token *next;
} token_t;

/* ── Command (one stage in a pipeline) ─────────────────────── */

typedef struct command {
    char  **argv;              /* Argument vector (NULL-terminated) */
    char   *infile;            /* Input redirect filename or NULL   */
    char   *outfile;           /* Output redirect filename or NULL  */
    int     append;            /* Append mode for outfile?         */
    char   *errfile;           /* Stderr redirect filename or NULL */
    struct command *next;      /* Next command in pipe chain        */
} command_t;

/* ── Global state ──────────────────────────────────────────── */

extern job_t *job_list;           /* Linked list of jobs            */
extern int    next_job_id;        /* Next job ID to assign          */
extern pid_t  shell_pgid;        /* Shell's own process group      */
extern int    shell_terminal;     /* Are we interactive?            */
extern int    last_exit_status;  /* $? for the last command        */
extern volatile sig_atomic_t sigint_received;  /* Ctrl+C flag     */
extern volatile sig_atomic_t sigtstp_received; /* Ctrl+Z flag     */

/* ── Core functions ────────────────────────────────────────── */

/* main.c */
void   shell_loop(void);

/* tokenize.c */
token_t *tokenize(const char *line);
void     token_free(token_t *tokens);

/* parse.c */
command_t *parse(token_t *tokens);
void       command_free(command_t *cmd);

/* execute.c */
int    execute(command_t *cmd);
int    exec_builtin(command_t *cmd);
char  *find_in_path(const char *name);

/* builtin.c */
int  builtin_cd(command_t *cmd);
int  builtin_exit(command_t *cmd);
int  builtin_pwd(command_t *cmd);
int  builtin_echo(command_t *cmd);
int  builtin_export(command_t *cmd);
int  builtin_unset(command_t *cmd);

/* job.c */
job_t *find_job(pid_t pgid);
job_t *find_job_by_id(int job_id);
void   update_jobs(void);
void   print_jobs(void);
int    fg_job(int job_id);
int    bg_job(int job_id);
job_t *add_job(pid_t pgid, const char *command, job_state_t state);
void   free_done_jobs(void);

/* signal.c */
void setup_signals(void);
void restore_signals(void);

#endif /* SHELL_H */