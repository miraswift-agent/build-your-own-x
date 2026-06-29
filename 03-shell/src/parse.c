/*
 * parse.c - Parser for the shell
 *
 * Converts a token list into a linked list of command_t structures.
 * Each command_t represents one stage of a pipeline.
 * Redirects are attached to their respective command stage.
 *
 * Grammar:
 *   pipeline  = command ('|' command)* ['&']
 *   command   = word+ [redirect]*
 *   redirect  = '>' word | '>>' word | '<' word | '2>' word
 */

#include "shell.h"

/* ── Helper: allocate a fresh command ──────────────────────── */

static command_t *command_new(void)
{
    command_t *cmd = calloc(1, sizeof(*cmd));
    if (!cmd) { perror("calloc"); exit(1); }
    return cmd;
}

/* ── Helper: grow argv array ────────────────────────────────── */

static void argv_push(command_t *cmd, const char *word)
{
    /* Count existing args */
    int count = 0;
    while (cmd->argv && cmd->argv[count]) count++;

    /* Grow array: count + 1 (new) + 1 (NULL) */
    cmd->argv = realloc(cmd->argv, (count + 2) * sizeof(char *));
    if (!cmd->argv) { perror("realloc"); exit(1); }

    cmd->argv[count]     = strdup(word);
    cmd->argv[count + 1] = NULL;
}

/* ── Main parser ───────────────────────────────────────────── */

command_t *parse(token_t *tokens)
{
    if (!tokens || tokens->type == TOK_EOF)
        return NULL;

    command_t *head = NULL;
    command_t *tail = NULL;
    command_t *cur  = command_new();
    int is_background = 0;

    for (token_t *t = tokens; t && t->type != TOK_EOF; t = t->next) {
        switch (t->type) {

        case TOK_WORD:
            argv_push(cur, t->value);
            break;

        case TOK_PIPE: {
            /* Finish current command, start a new pipeline stage */
            if (!cur->argv) {
                /* Pipe with no preceding command — syntax error */
                fprintf(stderr, "mira: syntax error near '|'\n");
                command_free(cur);
                command_free(head);
                return NULL;
            }
            /* Link into list */
            if (!head)
                head = cur;
            else
                tail->next = cur;
            tail = cur;

            /* Start new pipeline stage */
            cur = command_new();
            break;
        }

        case TOK_BG:
            is_background = 1;
            break;

        case TOK_REDIR_OUT:
            /* Next token must be a word (filename) */
            if (!t->next || t->next->type != TOK_WORD) {
                fprintf(stderr, "mira: syntax error near '>'\n");
                command_free(cur);
                command_free(head);
                return NULL;
            }
            free(cur->outfile);
            cur->outfile = strdup(t->next->value);
            cur->append  = 0;
            t = t->next;  /* skip the filename token */
            break;

        case TOK_REDIR_APPEND:
            if (!t->next || t->next->type != TOK_WORD) {
                fprintf(stderr, "mira: syntax error near '>>'\n");
                command_free(cur);
                command_free(head);
                return NULL;
            }
            free(cur->outfile);
            cur->outfile = strdup(t->next->value);
            cur->append  = 1;
            t = t->next;
            break;

        case TOK_REDIR_IN:
            if (!t->next || t->next->type != TOK_WORD) {
                fprintf(stderr, "mira: syntax error near '<'\n");
                command_free(cur);
                command_free(head);
                return NULL;
            }
            free(cur->infile);
            cur->infile = strdup(t->next->value);
            t = t->next;
            break;

        case TOK_REDIR_ERR:
            if (!t->next || t->next->type != TOK_WORD) {
                fprintf(stderr, "mira: syntax error near '2>'\n");
                command_free(cur);
                command_free(head);
                return NULL;
            }
            free(cur->errfile);
            cur->errfile = strdup(t->next->value);
            t = t->next;
            break;

        case TOK_EOF:
            break;   /* Already handled by loop condition */
        }
    }

    /* Attach last command */
    if (cur->argv) {
        if (!head)
            head = cur;
        else
            tail->next = cur;
    } else {
        /* Empty trailing command after pipe — only if we had a pipe */
        if (tail) {
            fprintf(stderr, "mira: syntax error: trailing pipe\n");
            command_free(cur);
            command_free(head);
            return NULL;
        }
        command_free(cur);
    }

    /* Mark background: store as a flag on the pipeline.
     * We use a convention: if is_background, set outfile to a special
     * sentinel... Actually, let's use the command struct's append field
     * as a hack? No — we pass it through the return differently.
     * Better: we encode bg in the command chain by setting the tail's
     * append field to a magic value. Actually, let's just add a global
     * that the executor checks.
     *
     * Cleanest approach: the caller (shell_loop) checks is_background
     * from the token stream directly. But we're inside parse()...
     *
     * Simplest: store bg flag on the head command. We'll use a
     * convention where append=-1 means "background". No, that's ugly.
     *
     * Best: add a bg field to command_t. But we declared it already...
     * Let's just use a global flag that execute() checks.
     */

    /* We set a global flag for background — the executor reads it */
    extern int parse_background;
    parse_background = is_background;

    return head;
}

/* Defined in main.c */

/* ── Free a command list ────────────────────────────────────── */

void command_free(command_t *cmd)
{
    while (cmd) {
        command_t *next = cmd->next;
        if (cmd->argv) {
            for (int i = 0; cmd->argv[i]; i++)
                free(cmd->argv[i]);
            free(cmd->argv);
        }
        free(cmd->infile);
        free(cmd->outfile);
        free(cmd->errfile);
        free(cmd);
        cmd = next;
    }
}