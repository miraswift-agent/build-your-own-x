/*
 * signal.c - Signal handlers for the shell
 *
 * Handles:
 *   - SIGINT (Ctrl+C): forwarded to foreground process group
 *   - SIGTSTP (Ctrl+Z): stops foreground process group
 *   - SIGCHLD: reaps background children
 */

#include "shell.h"

/* ── Global flags set by signal handlers ───────────────────── */
/* Defined in main.c, referenced via shell.h externs */

/* ── SIGINT handler ─────────────────────────────────────────── */

static void sigint_handler(int sig)
{
    (void)sig;
    sigint_received = 1;
}

/* ── SIGTSTP handler ────────────────────────────────────────── */

static void sigtstp_handler(int sig)
{
    (void)sig;
    sigtstp_received = 1;
}

/* ── SIGCHLD handler ────────────────────────────────────────── */

static void sigchld_handler(int sig)
{
    (void)sig;
    /* Just kick the wait loop — update_jobs() does the real work */
}

/* ── Install signal handlers ────────────────────────────────── */

void setup_signals(void)
{
    struct sigaction sa;

    /* SIGINT: interrupt foreground, but don't kill the shell */
    sa.sa_handler = sigint_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGINT, &sa, NULL);

    /* SIGTSTP: stop foreground, but don't stop the shell */
    sa.sa_handler = sigtstp_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGTSTP, &sa, NULL);

    /* SIGCHLD: reap children */
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigaction(SIGCHLD, &sa, NULL);

    /* Ignore SIGQUIT (like real shells do) */
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGQUIT, &sa, NULL);
}

/* ── Restore default signal handling ────────────────────────── */

void restore_signals(void)
{
    struct sigaction sa;
    sa.sa_handler = SIG_DFL;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTSTP, &sa, NULL);
    sigaction(SIGCHLD, &sa, NULL);
    sigaction(SIGQUIT, &sa, NULL);
}