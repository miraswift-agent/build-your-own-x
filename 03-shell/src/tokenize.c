/*
 * tokenize.c - Lexer for the shell
 *
 * Converts a command line string into a linked list of tokens.
 * Handles:
 *   - Quoted strings (single and double quotes)
 *   - Backslash escapes
 *   - Operators: |, &, >, >>, <, 2>
 *   - Words (unquoted sequences)
 */

#include "shell.h"

/* ── Helper: allocate a new token ──────────────────────────── */

static token_t *token_new(token_type_t type, char *value)
{
    token_t *t = malloc(sizeof(*t));
    if (!t) {
        perror("malloc");
        exit(1);
    }
    t->type  = type;
    t->value = value;   /* May be NULL for non-word tokens */
    t->next  = NULL;
    return t;
}

/* ── Helper: append token to list, return new tail ─────────── */

static token_t *token_append(token_t *tail, token_t *node)
{
    if (tail)
        tail->next = node;
    return node;
}

/* ── Helper: grow a dynamic string buffer ──────────────────── */

typedef struct {
    char  *data;
    size_t len;
    size_t cap;
} strbuf_t;

static void sb_init(strbuf_t *sb)
{
    sb->cap = 64;
    sb->data = malloc(sb->cap);
    if (!sb->data) { perror("malloc"); exit(1); }
    sb->len = 0;
    sb->data[0] = '\0';
}

static void sb_push(strbuf_t *sb, char ch)
{
    if (sb->len + 1 >= sb->cap) {
        size_t new_cap = sb->cap ? sb->cap * 2 : 64;
        char *new_data = realloc(sb->data, new_cap);
        if (!new_data) { perror("realloc"); exit(1); }
        sb->data = new_data;
        sb->cap = new_cap;
    }
    sb->data[sb->len++] = ch;
    sb->data[sb->len] = '\0';
}

static char *sb_finish(strbuf_t *sb)
{
    /* Caller owns the returned string */
    char *result = sb->data;
    /* Reset buffer for reuse (sb_push will re-init on next use) */
    sb->data = NULL;
    sb->len  = 0;
    sb->cap = 0;
    return result;
}

static void sb_free(strbuf_t *sb)
{
    free(sb->data);
    sb->data = NULL;
    sb->len  = 0;
    sb->cap = 0;
}

/* ── Main tokenizer ────────────────────────────────────────── */

token_t *tokenize(const char *line)
{
    token_t   head = {0};
    token_t  *tail = &head;
    strbuf_t  sb;
    int       in_word = 0;    /* Currently accumulating a word? */
    int       i       = 0;

    sb_init(&sb);

    while (line[i]) {
        char ch = line[i];

        /* ── Backslash escape ──────────────────────────────── */
        if (ch == '\\' && line[i + 1] != '\0') {
            sb_push(&sb, line[i + 1]);
            i += 2;
            in_word = 1;
            continue;
        }

        /* ── Double-quoted string ─────────────────────────── */
        if (ch == '"') {
            i++;   /* skip opening quote */
            while (line[i] && line[i] != '"') {
                if (line[i] == '\\' && line[i + 1] == '"') {
                    sb_push(&sb, '"');
                    i += 2;
                } else if (line[i] == '\\' && line[i + 1] == '\\') {
                    sb_push(&sb, '\\');
                    i += 2;
                } else if (line[i] == '\\' && line[i + 1] == '$') {
                    sb_push(&sb, '$');
                    i += 2;
                } else {
                    sb_push(&sb, line[i]);
                    i++;
                }
            }
            if (line[i] == '"') i++;  /* skip closing quote */
            in_word = 1;
            continue;
        }

        /* ── Single-quoted string ──────────────────────────── */
        if (ch == '\'') {
            i++;   /* skip opening quote */
            while (line[i] && line[i] != '\'') {
                sb_push(&sb, line[i]);
                i++;
            }
            if (line[i] == '\'') i++;  /* skip closing quote */
            in_word = 1;
            continue;
        }

        /* ── Whitespace ────────────────────────────────────── */
        if (isspace((unsigned char)ch)) {
            if (in_word) {
                /* Flush current word */
                tail = token_append(tail, token_new(TOK_WORD, sb_finish(&sb)));
                in_word = 0;
            }
            i++;
            continue;
        }

        /* ── 2> (stderr redirect — must check before digit) ── */
        if (ch == '2' && line[i + 1] == '>') {
            if (in_word) {
                tail = token_append(tail, token_new(TOK_WORD, sb_finish(&sb)));
                in_word = 0;
            }
            tail = token_append(tail, token_new(TOK_REDIR_ERR, NULL));
            i += 2;
            continue;
        }

        /* ── >> (append redirect) ──────────────────────────── */
        if (ch == '>' && line[i + 1] == '>') {
            if (in_word) {
                tail = token_append(tail, token_new(TOK_WORD, sb_finish(&sb)));
                in_word = 0;
            }
            tail = token_append(tail, token_new(TOK_REDIR_APPEND, NULL));
            i += 2;
            continue;
        }

        /* ── > (output redirect) ───────────────────────────── */
        if (ch == '>') {
            if (in_word) {
                tail = token_append(tail, token_new(TOK_WORD, sb_finish(&sb)));
                in_word = 0;
            }
            tail = token_append(tail, token_new(TOK_REDIR_OUT, NULL));
            i++;
            continue;
        }

        /* ── < (input redirect) ────────────────────────────── */
        if (ch == '<') {
            if (in_word) {
                tail = token_append(tail, token_new(TOK_WORD, sb_finish(&sb)));
                in_word = 0;
            }
            tail = token_append(tail, token_new(TOK_REDIR_IN, NULL));
            i++;
            continue;
        }

        /* ── | (pipe) ───────────────────────────────────────── */
        if (ch == '|') {
            if (in_word) {
                tail = token_append(tail, token_new(TOK_WORD, sb_finish(&sb)));
                in_word = 0;
            }
            tail = token_append(tail, token_new(TOK_PIPE, NULL));
            i++;
            continue;
        }

        /* ── & (background) ────────────────────────────────── */
        if (ch == '&') {
            if (in_word) {
                tail = token_append(tail, token_new(TOK_WORD, sb_finish(&sb)));
                in_word = 0;
            }
            tail = token_append(tail, token_new(TOK_BG, NULL));
            i++;
            continue;
        }

        /* ── Regular character ──────────────────────────────── */
        sb_push(&sb, ch);
        in_word = 1;
        i++;
    }

    /* Flush any remaining word */
    if (in_word) {
        tail = token_append(tail, token_new(TOK_WORD, sb_finish(&sb)));
    }

    sb_free(&sb);

    /* Append EOF sentinel */
    tail = token_append(tail, token_new(TOK_EOF, NULL));

    return head.next;   /* head was a dummy node */
}

/* ── Free a token list ─────────────────────────────────────── */

void token_free(token_t *tokens)
{
    while (tokens) {
        token_t *next = tokens->next;
        free(tokens->value);
        free(tokens);
        tokens = next;
    }
}