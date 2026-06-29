/*
 * job.c - Job control for the shell
 *
 * Manages background and stopped jobs:
 *   - Job table (linked list of job_t)
 *   - Adding/removing jobs
 *   - fg/bg commands
 *   - Reaping finished children
 */

#include "shell.h"

/* ── Global state ──────────────────────────────────────────── */
/* Defined in main.c, referenced everywhere via shell.h externs */

/* ── Add a new job ──────────────────────────────────────────── */

job_t *add_job(pid_t pgid, const char *command, job_state_t state)
{
    job_t *j = malloc(sizeof(*j));
    if (!j) { perror("malloc"); exit(1); }

    j->job_id  = next_job_id++;
    j->pgid    = pgid;
    j->command = strdup(command);
    j->state   = state;
    j->notified = 0;
    j->next    = NULL;

    /* Append to list */
    if (!job_list) {
        job_list = j;
    } else {
        job_t *tail = job_list;
        while (tail->next) tail = tail->next;
        tail->next = j;
    }

    return j;
}

/* ── Find job by process group ID ──────────────────────────── */

job_t *find_job(pid_t pgid)
{
    for (job_t *j = job_list; j; j = j->next) {
        if (j->pgid == pgid)
            return j;
    }
    return NULL;
}

/* ── Find job by job ID ────────────────────────────────────── */

job_t *find_job_by_id(int job_id)
{
    for (job_t *j = job_list; j; j = j->next) {
        if (j->job_id == job_id)
            return j;
    }
    return NULL;
}

/* ── Check for completed/stopped jobs (non-blocking) ──────── */

void update_jobs(void)
{
    pid_t pid;
    int   status;

    /* Reap any finished children without blocking */
    while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED)) > 0) {
        job_t *j = find_job(pid);
        if (!j) continue;  /* Not in our job table */

        if (WIFSTOPPED(status)) {
            j->state   = JOB_STOPPED;
            j->notified = 0;
            fprintf(stderr, "\n[%d]+  Stopped    %s\n", j->job_id, j->command);
        } else if (WIFEXITED(status) || WIFSIGNALED(status)) {
            j->state   = JOB_DONE;
            j->notified = 0;
            if (WIFEXITED(status))
                last_exit_status = WEXITSTATUS(status);
            else
                last_exit_status = 128 + WTERMSIG(status);
        }
    }
}

/* ── Print all active jobs ─────────────────────────────────── */

void print_jobs(void)
{
    for (job_t *j = job_list; j; j = j->next) {
        const char *state_str;
        switch (j->state) {
        case JOB_RUNNING: state_str = "Running";   break;
        case JOB_STOPPED: state_str = "Stopped";   break;
        case JOB_DONE:    state_str = "Done";       break;
        default:          state_str = "Unknown";   break;
        }
        fprintf(stderr, "[%d]  %s    %s\n", j->job_id, state_str, j->command);
    }
}

/* ── Bring a job to the foreground ─────────────────────────── */

int fg_job(int job_id)
{
    job_t *j = find_job_by_id(job_id);
    if (!j) {
        fprintf(stderr, "mira: fg: %%%d: no such job\n", job_id);
        return 1;
    }

    /* Give the job's process group terminal control */
    if (shell_terminal && tcsetpgrp(STDIN_FILENO, j->pgid) < 0) {
        perror("tcsetpgrp");
    }

    /* If stopped, send SIGCONT */
    if (j->state == JOB_STOPPED) {
        kill(-j->pgid, SIGCONT);
    }

    j->state = JOB_RUNNING;

    /* Wait for it to finish or stop */
    int status;
    pid_t pid;
    do {
        pid = waitpid(-j->pgid, &status, WUNTRACED);
    } while (pid != j->pgid && pid > 0);

    /* Shell regains terminal control */
    if (shell_terminal) {
        tcsetpgrp(STDIN_FILENO, shell_pgid);
    }

    if (WIFSTOPPED(status)) {
        j->state    = JOB_STOPPED;
        j->notified = 0;
        fprintf(stderr, "\n[%d]+  Stopped    %s\n", j->job_id, j->command);
    } else if (WIFEXITED(status)) {
        last_exit_status = WEXITSTATUS(status);
        j->state = JOB_DONE;
    } else if (WIFSIGNALED(status)) {
        last_exit_status = 128 + WTERMSIG(status);
        j->state = JOB_DONE;
    }

    free_done_jobs();
    return last_exit_status;
}

/* ── Continue a job in the background ──────────────────────── */

int bg_job(int job_id)
{
    job_t *j = find_job_by_id(job_id);
    if (!j) {
        fprintf(stderr, "mira: bg: %%%d: no such job\n", job_id);
        return 1;
    }

    if (j->state == JOB_STOPPED) {
        j->state = JOB_RUNNING;
        kill(-j->pgid, SIGCONT);
        fprintf(stderr, "[%d]+ %s &\n", j->job_id, j->command);
    } else {
        fprintf(stderr, "mira: bg: job %d already running\n", job_id);
    }

    return 0;
}

/* ── Remove completed jobs from the list ────────────────────── */

void free_done_jobs(void)
{
    job_t **pp = &job_list;
    while (*pp) {
        job_t *j = *pp;
        if (j->state == JOB_DONE) {
            if (!j->notified) {
                fprintf(stderr, "[%d]+  Done       %s\n", j->job_id, j->command);
                j->notified = 1;
            }
            *pp = j->next;
            free(j->command);
            free(j);
        } else {
            pp = &j->next;
        }
    }
}