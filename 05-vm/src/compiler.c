/*
 * Lantern VM — Stage 3: Compiler
 *
 * Scanner + recursive-descent parser + AST + tree-walking code generator.
 * Emits Stage 2 assembler text, then feeds it to asm_parse.
 */

#define _POSIX_C_SOURCE 200809L

#include "compiler.h"
#include <ctype.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================
 * AST
 * ============================================================ */

typedef enum {
    AST_PROGRAM,
    AST_FUNCTION,
    AST_BLOCK,
    AST_VAR,
    AST_ASSIGN,
    AST_IF,
    AST_WHILE,
    AST_RETURN,
    AST_EXPR_STMT,
    AST_BINARY,
    AST_UNARY,
    AST_CALL,
    AST_LITERAL,
    AST_VARIABLE,
    AST_GROUPING,
} AstType;

struct AstNode {
    AstType type;
    int line;
    AstNode *next; /* sibling link for variadic lists */
    union {
        struct { AstNode *first_func; } program;
        struct { const char *name; AstNode *params; AstNode *body; } function;
        struct { AstNode *first_stmt; } block;
        struct { const char *name; AstNode *init; } var;
        struct { const char *name; AstNode *value; } assign;
        struct { AstNode *cond; AstNode *then_branch; AstNode *else_branch; } ifstmt;
        struct { AstNode *cond; AstNode *body; } whilestmt;
        struct { AstNode *value; bool has_value; } returnstmt;
        struct { AstNode *expr; } expr_stmt;
        struct { int op; AstNode *left; AstNode *right; } binary;
        struct { int op; AstNode *operand; } unary;
        struct { const char *name; AstNode *args; int argc; } call;
        struct { Value value; } literal;
        struct { const char *name; } variable;
        struct { AstNode *expr; } grouping;
    } as;
};

struct AstAlloc {
    void *ptr;
    AstAlloc *next;
};

static void *ast_alloc(Compiler *c, size_t size) {
    void *p = malloc(size);
    if (!p) {
        snprintf(c->error_msg, sizeof(c->error_msg), "Out of memory");
        c->error = 1;
        return NULL;
    }

    AstAlloc *node = malloc(sizeof(AstAlloc));
    if (!node) {
        free(p);
        snprintf(c->error_msg, sizeof(c->error_msg), "Out of memory");
        c->error = 1;
        return NULL;
    }

    node->ptr = p;
    node->next = c->ast_allocs;
    c->ast_allocs = node;
    return p;
}

static AstNode *ast_new(Compiler *c, AstType type, int line) {
    AstNode *node = ast_alloc(c, sizeof(AstNode));
    if (!node) return NULL;
    memset(node, 0, sizeof(AstNode));
    node->type = type;
    node->line = line;
    return node;
}

static char *ast_strndup(Compiler *c, const char *s, int len) {
    char *p = ast_alloc(c, (size_t)len + 1);
    if (!p) return NULL;
    memcpy(p, s, (size_t)len);
    p[len] = '\0';
    return p;
}

/* ============================================================
 * Scanner
 * ============================================================ */

typedef enum {
    TOK_EOF,
    TOK_ERROR,
    TOK_FUNC,
    TOK_VAR,
    TOK_IF,
    TOK_ELSE,
    TOK_WHILE,
    TOK_RETURN,
    TOK_TRUE,
    TOK_FALSE,
    TOK_NULL,
    TOK_IDENT,
    TOK_NUMBER,
    TOK_FLOAT,
    TOK_LPAREN,
    TOK_RPAREN,
    TOK_LBRACE,
    TOK_RBRACE,
    TOK_SEMICOLON,
    TOK_COMMA,
    TOK_ASSIGN,
    TOK_EQ,
    TOK_NE,
    TOK_LT,
    TOK_GT,
    TOK_LE,
    TOK_GE,
    TOK_PLUS,
    TOK_MINUS,
    TOK_STAR,
    TOK_SLASH,
    TOK_PERCENT,
    TOK_NOT,
} TokenType;

typedef struct {
    TokenType type;
    const char *start;
    int length;
    int line;
} Token;

typedef struct {
    const char *start;
    const char *current;
    int line;
    Compiler *c;
} Scanner;

static bool is_at_end(Scanner *s) {
    return *s->current == '\0';
}

static char advance_scanner(Scanner *s) {
    s->current++;
    return s->current[-1];
}

static char peek(Scanner *s) {
    return *s->current;
}

static char peek_next(Scanner *s) {
    if (is_at_end(s)) return '\0';
    return s->current[1];
}

static bool match_char(Scanner *s, char expected) {
    if (is_at_end(s)) return false;
    if (*s->current != expected) return false;
    s->current++;
    return true;
}

static Token make_token(Scanner *s, TokenType type) {
    return (Token){
        .type = type,
        .start = s->start,
        .length = (int)(s->current - s->start),
        .line = s->line,
    };
}

static Token error_token(Scanner *s, const char *msg) {
    return (Token){
        .type = TOK_ERROR,
        .start = msg,
        .length = (int)strlen(msg),
        .line = s->line,
    };
}

static TokenType identifier_type(Scanner *s) {
    int len = (int)(s->current - s->start);
    switch (s->start[0]) {
    case 'e':
        if (len == 4 && strncmp(s->start, "else", 4) == 0) return TOK_ELSE;
        break;
    case 'f':
        if (len == 4 && strncmp(s->start, "func", 4) == 0) return TOK_FUNC;
        if (len == 5 && strncmp(s->start, "false", 5) == 0) return TOK_FALSE;
        break;
    case 'i':
        if (len == 2 && strncmp(s->start, "if", 2) == 0) return TOK_IF;
        break;
    case 'n':
        if (len == 4 && strncmp(s->start, "null", 4) == 0) return TOK_NULL;
        break;
    case 'r':
        if (len == 6 && strncmp(s->start, "return", 6) == 0) return TOK_RETURN;
        break;
    case 't':
        if (len == 4 && strncmp(s->start, "true", 4) == 0) return TOK_TRUE;
        break;
    case 'v':
        if (len == 3 && strncmp(s->start, "var", 3) == 0) return TOK_VAR;
        break;
    case 'w':
        if (len == 5 && strncmp(s->start, "while", 5) == 0) return TOK_WHILE;
        break;
    }
    return TOK_IDENT;
}

static Token skip(Scanner *s) {
    for (;;) {
        char c = peek(s);
        switch (c) {
        case ' ':
        case '\r':
        case '\t':
            advance_scanner(s);
            break;
        case '\n':
            s->line++;
            advance_scanner(s);
            break;
        case '/':
            if (peek_next(s) == '/') {
                while (peek(s) != '\n' && !is_at_end(s)) advance_scanner(s);
            } else if (peek_next(s) == '*') {
                advance_scanner(s);
                advance_scanner(s);
                while (!(peek(s) == '*' && peek_next(s) == '/') && !is_at_end(s)) {
                    if (peek(s) == '\n') s->line++;
                    advance_scanner(s);
                }
                if (is_at_end(s)) {
                    return error_token(s, "Unterminated block comment");
                }
                advance_scanner(s);
                advance_scanner(s);
            } else {
                return make_token(s, TOK_EOF);
            }
            break;
        default:
            return make_token(s, TOK_EOF);
        }
    }
}

static Token scan_token(Scanner *s) {
    if (is_at_end(s)) return make_token(s, TOK_EOF);

    Token err = skip(s);
    if (err.type == TOK_ERROR) return err;
    if (is_at_end(s)) return make_token(s, TOK_EOF);

    s->start = s->current;
    char c = advance_scanner(s);

    if (isalpha((unsigned char)c) || c == '_') {
        while (isalnum((unsigned char)peek(s)) || peek(s) == '_') advance_scanner(s);
        return make_token(s, identifier_type(s));
    }

    if (isdigit((unsigned char)c)) {
        while (isdigit((unsigned char)peek(s))) advance_scanner(s);
        if (peek(s) == '.') {
            advance_scanner(s);
            while (isdigit((unsigned char)peek(s))) advance_scanner(s);
            return make_token(s, TOK_FLOAT);
        }
        return make_token(s, TOK_NUMBER);
    }

    switch (c) {
    case '(': return make_token(s, TOK_LPAREN);
    case ')': return make_token(s, TOK_RPAREN);
    case '{': return make_token(s, TOK_LBRACE);
    case '}': return make_token(s, TOK_RBRACE);
    case ';': return make_token(s, TOK_SEMICOLON);
    case ',': return make_token(s, TOK_COMMA);
    case '+': return make_token(s, TOK_PLUS);
    case '-': return make_token(s, TOK_MINUS);
    case '*': return make_token(s, TOK_STAR);
    case '/': return make_token(s, TOK_SLASH);
    case '%': return make_token(s, TOK_PERCENT);
    case '!':
        return make_token(s, match_char(s, '=') ? TOK_NE : TOK_NOT);
    case '=':
        return make_token(s, match_char(s, '=') ? TOK_EQ : TOK_ASSIGN);
    case '<':
        return make_token(s, match_char(s, '=') ? TOK_LE : TOK_LT);
    case '>':
        return make_token(s, match_char(s, '=') ? TOK_GE : TOK_GT);
    }

    return error_token(s, "Unexpected character");
}

/* ============================================================
 * Parser
 * ============================================================ */

typedef struct {
    Scanner scanner;
    Token current;
    Token previous;
    Compiler *c;
    bool had_error;
    bool panic_mode;
} Parser;

static void advance_parser(Parser *p) {
    p->previous = p->current;
    p->current = scan_token(&p->scanner);
    if (p->current.type == TOK_ERROR) {
        if (!p->panic_mode) {
            p->panic_mode = true;
            p->had_error = true;
            p->c->error = 1;
            snprintf(p->c->error_msg, sizeof(p->c->error_msg),
                     "Line %d: %s", p->current.line, p->current.start);
        }
    }
}

static bool check(Parser *p, TokenType type) {
    return p->current.type == type;
}

static bool match(Parser *p, TokenType type) {
    if (!check(p, type)) return false;
    advance_parser(p);
    return true;
}

static bool consume(Parser *p, TokenType type, const char *msg) {
    if (p->current.type == type) {
        advance_parser(p);
        return true;
    }

    if (!p->panic_mode) {
        p->panic_mode = true;
        p->had_error = true;
        p->c->error = 1;
        snprintf(p->c->error_msg, sizeof(p->c->error_msg),
                 "Line %d: %s", p->current.line, msg);
    }
    return false;
}

static void synchronize(Parser *p) {
    if (!p->panic_mode) return;
    p->panic_mode = false;

    while (p->current.type != TOK_EOF) {
        if (p->previous.type == TOK_SEMICOLON) return;
        switch (p->current.type) {
        case TOK_FUNC:
        case TOK_VAR:
        case TOK_IF:
        case TOK_WHILE:
        case TOK_RETURN:
        case TOK_RBRACE:
            return;
        default:
            break;
        }
        advance_parser(p);
    }
}

static AstNode *parse_expression(Parser *p);

static AstNode *parse_primary(Parser *p) {
    if (match(p, TOK_TRUE)) {
        AstNode *node = ast_new(p->c, AST_LITERAL, p->previous.line);
        if (node) node->as.literal.value = BOOL_VAL(true);
        return node;
    }
    if (match(p, TOK_FALSE)) {
        AstNode *node = ast_new(p->c, AST_LITERAL, p->previous.line);
        if (node) node->as.literal.value = BOOL_VAL(false);
        return node;
    }
    if (match(p, TOK_NULL)) {
        AstNode *node = ast_new(p->c, AST_LITERAL, p->previous.line);
        if (node) node->as.literal.value = NULL_VAL;
        return node;
    }
    if (match(p, TOK_NUMBER)) {
        AstNode *node = ast_new(p->c, AST_LITERAL, p->previous.line);
        if (node) {
            char buf[64];
            int len = p->previous.length < 63 ? p->previous.length : 63;
            memcpy(buf, p->previous.start, (size_t)len);
            buf[len] = '\0';
            node->as.literal.value = INT_VAL(strtoll(buf, NULL, 10));
        }
        return node;
    }
    if (match(p, TOK_FLOAT)) {
        AstNode *node = ast_new(p->c, AST_LITERAL, p->previous.line);
        if (node) {
            char buf[64];
            int len = p->previous.length < 63 ? p->previous.length : 63;
            memcpy(buf, p->previous.start, (size_t)len);
            buf[len] = '\0';
            node->as.literal.value = FLOAT_VAL(strtod(buf, NULL));
        }
        return node;
    }
    if (match(p, TOK_IDENT)) {
        AstNode *node = ast_new(p->c, AST_VARIABLE, p->previous.line);
        if (node) {
            node->as.variable.name = ast_strndup(p->c, p->previous.start, p->previous.length);
        }
        return node;
    }
    if (match(p, TOK_LPAREN)) {
        int line = p->previous.line;
        AstNode *expr = parse_expression(p);
        if (!expr) return NULL;
        if (!consume(p, TOK_RPAREN, "Expected ')' after expression")) return NULL;
        AstNode *node = ast_new(p->c, AST_GROUPING, line);
        if (node) node->as.grouping.expr = expr;
        return node;
    }

    if (!p->panic_mode) {
        p->panic_mode = true;
        p->had_error = true;
        p->c->error = 1;
        snprintf(p->c->error_msg, sizeof(p->c->error_msg),
                 "Line %d: Expected expression", p->current.line);
    }
    return NULL;
}

static AstNode *parse_call(Parser *p) {
    AstNode *primary = parse_primary(p);
    if (!primary) return NULL;

    if (match(p, TOK_LPAREN)) {
        if (primary->type != AST_VARIABLE) {
            if (!p->panic_mode) {
                p->panic_mode = true;
                p->had_error = true;
                p->c->error = 1;
                snprintf(p->c->error_msg, sizeof(p->c->error_msg),
                         "Line %d: Can only call functions by name", p->current.line);
            }
            return NULL;
        }

        const char *name = primary->as.variable.name;
        int line = primary->line;
        AstNode *args = NULL;
        AstNode *tail = NULL;
        int argc = 0;

        if (!check(p, TOK_RPAREN)) {
            do {
                AstNode *arg = parse_expression(p);
                if (!arg) return NULL;
                if (tail) tail->next = arg;
                else args = arg;
                tail = arg;
                argc++;
            } while (match(p, TOK_COMMA));
        }

        if (!consume(p, TOK_RPAREN, "Expected ')' after arguments")) return NULL;

        AstNode *node = ast_new(p->c, AST_CALL, line);
        if (node) {
            node->as.call.name = name;
            node->as.call.args = args;
            node->as.call.argc = argc;
        }
        return node;
    }

    return primary;
}

static AstNode *parse_unary(Parser *p) {
    if (match(p, TOK_MINUS) || match(p, TOK_NOT)) {
        int op = p->previous.type;
        int line = p->previous.line;
        AstNode *operand = parse_unary(p);
        if (!operand) return NULL;
        AstNode *node = ast_new(p->c, AST_UNARY, line);
        if (node) {
            node->as.unary.op = op;
            node->as.unary.operand = operand;
        }
        return node;
    }
    return parse_call(p);
}

static AstNode *parse_multiplicative(Parser *p) {
    AstNode *left = parse_unary(p);
    if (!left) return NULL;

    while (match(p, TOK_STAR) || match(p, TOK_SLASH) || match(p, TOK_PERCENT)) {
        int op = p->previous.type;
        int line = p->previous.line;
        AstNode *right = parse_unary(p);
        if (!right) return NULL;
        AstNode *node = ast_new(p->c, AST_BINARY, line);
        if (node) {
            node->as.binary.op = op;
            node->as.binary.left = left;
            node->as.binary.right = right;
        }
        left = node;
    }
    return left;
}

static AstNode *parse_additive(Parser *p) {
    AstNode *left = parse_multiplicative(p);
    if (!left) return NULL;

    while (match(p, TOK_PLUS) || match(p, TOK_MINUS)) {
        int op = p->previous.type;
        int line = p->previous.line;
        AstNode *right = parse_multiplicative(p);
        if (!right) return NULL;
        AstNode *node = ast_new(p->c, AST_BINARY, line);
        if (node) {
            node->as.binary.op = op;
            node->as.binary.left = left;
            node->as.binary.right = right;
        }
        left = node;
    }
    return left;
}

static AstNode *parse_comparison(Parser *p) {
    AstNode *left = parse_additive(p);
    if (!left) return NULL;

    while (match(p, TOK_LT) || match(p, TOK_GT) || match(p, TOK_LE) || match(p, TOK_GE)) {
        int op = p->previous.type;
        int line = p->previous.line;
        AstNode *right = parse_additive(p);
        if (!right) return NULL;
        AstNode *node = ast_new(p->c, AST_BINARY, line);
        if (node) {
            node->as.binary.op = op;
            node->as.binary.left = left;
            node->as.binary.right = right;
        }
        left = node;
    }
    return left;
}

static AstNode *parse_equality(Parser *p) {
    AstNode *left = parse_comparison(p);
    if (!left) return NULL;

    while (match(p, TOK_EQ) || match(p, TOK_NE)) {
        int op = p->previous.type;
        int line = p->previous.line;
        AstNode *right = parse_comparison(p);
        if (!right) return NULL;
        AstNode *node = ast_new(p->c, AST_BINARY, line);
        if (node) {
            node->as.binary.op = op;
            node->as.binary.left = left;
            node->as.binary.right = right;
        }
        left = node;
    }
    return left;
}

static AstNode *parse_expression(Parser *p) {
    return parse_equality(p);
}

static AstNode *parse_block(Parser *p);
static AstNode *parse_statement(Parser *p);

static AstNode *parse_var_decl(Parser *p) {
    int line = p->previous.line;
    if (!consume(p, TOK_IDENT, "Expected variable name")) return NULL;
    const char *name = ast_strndup(p->c, p->previous.start, p->previous.length);
    if (p->c->error) return NULL;

    if (!consume(p, TOK_ASSIGN, "Expected '=' after variable name")) return NULL;

    AstNode *init = parse_expression(p);
    if (!init) return NULL;

    if (!consume(p, TOK_SEMICOLON, "Expected ';' after variable declaration")) return NULL;

    AstNode *node = ast_new(p->c, AST_VAR, line);
    if (node) {
        node->as.var.name = name;
        node->as.var.init = init;
    }
    return node;
}

static AstNode *parse_assign_stmt(Parser *p) {
    int line = p->current.line;
    if (!consume(p, TOK_IDENT, "Expected variable name")) return NULL;
    const char *name = ast_strndup(p->c, p->previous.start, p->previous.length);
    if (p->c->error) return NULL;

    if (!consume(p, TOK_ASSIGN, "Expected '=' after variable name")) return NULL;

    AstNode *value = parse_expression(p);
    if (!value) return NULL;

    if (!consume(p, TOK_SEMICOLON, "Expected ';' after assignment")) return NULL;

    AstNode *node = ast_new(p->c, AST_ASSIGN, line);
    if (node) {
        node->as.assign.name = name;
        node->as.assign.value = value;
    }
    return node;
}

static AstNode *parse_if_stmt(Parser *p) {
    int line = p->previous.line;
    if (!consume(p, TOK_LPAREN, "Expected '(' after 'if'")) return NULL;
    AstNode *cond = parse_expression(p);
    if (!cond) return NULL;
    if (!consume(p, TOK_RPAREN, "Expected ')' after condition")) return NULL;

    AstNode *then_branch = parse_block(p);
    if (!then_branch) return NULL;

    AstNode *else_branch = NULL;
    if (match(p, TOK_ELSE)) {
        else_branch = parse_block(p);
        if (!else_branch) return NULL;
    }

    AstNode *node = ast_new(p->c, AST_IF, line);
    if (node) {
        node->as.ifstmt.cond = cond;
        node->as.ifstmt.then_branch = then_branch;
        node->as.ifstmt.else_branch = else_branch;
    }
    return node;
}

static AstNode *parse_while_stmt(Parser *p) {
    int line = p->previous.line;
    if (!consume(p, TOK_LPAREN, "Expected '(' after 'while'")) return NULL;
    AstNode *cond = parse_expression(p);
    if (!cond) return NULL;
    if (!consume(p, TOK_RPAREN, "Expected ')' after condition")) return NULL;

    AstNode *body = parse_block(p);
    if (!body) return NULL;

    AstNode *node = ast_new(p->c, AST_WHILE, line);
    if (node) {
        node->as.whilestmt.cond = cond;
        node->as.whilestmt.body = body;
    }
    return node;
}

static AstNode *parse_return_stmt(Parser *p) {
    int line = p->previous.line;
    bool has_value = !check(p, TOK_SEMICOLON);
    AstNode *value = NULL;

    if (has_value) {
        value = parse_expression(p);
        if (!value) return NULL;
    }

    if (!consume(p, TOK_SEMICOLON, "Expected ';' after return")) return NULL;

    AstNode *node = ast_new(p->c, AST_RETURN, line);
    if (node) {
        node->as.returnstmt.value = value;
        node->as.returnstmt.has_value = has_value;
    }
    return node;
}

static AstNode *parse_expr_stmt(Parser *p) {
    int line = p->current.line;
    AstNode *expr = parse_expression(p);
    if (!expr) return NULL;
    if (!consume(p, TOK_SEMICOLON, "Expected ';' after expression")) return NULL;

    AstNode *node = ast_new(p->c, AST_EXPR_STMT, line);
    if (node) node->as.expr_stmt.expr = expr;
    return node;
}

static Token peek_next_token(Parser *p) {
    Scanner saved = p->scanner;
    return scan_token(&saved);
}

static AstNode *parse_statement(Parser *p) {
    if (match(p, TOK_VAR)) return parse_var_decl(p);
    if (match(p, TOK_IF)) return parse_if_stmt(p);
    if (match(p, TOK_WHILE)) return parse_while_stmt(p);
    if (match(p, TOK_RETURN)) return parse_return_stmt(p);
    if (check(p, TOK_IDENT) && peek_next_token(p).type == TOK_ASSIGN) {
        return parse_assign_stmt(p);
    }
    if (check(p, TOK_LBRACE)) return parse_block(p);
    return parse_expr_stmt(p);
}

static AstNode *parse_block(Parser *p) {
    int line = p->current.line;
    if (!consume(p, TOK_LBRACE, "Expected '{'")) return NULL;

    AstNode *block = ast_new(p->c, AST_BLOCK, line);
    if (!block) return NULL;
    AstNode *tail = NULL;

    while (!check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
        if (p->panic_mode) synchronize(p);
        AstNode *stmt = parse_statement(p);
        if (stmt) {
            if (tail) tail->next = stmt;
            else block->as.block.first_stmt = stmt;
            tail = stmt;
        } else if (!p->panic_mode) {
            break;
        }
    }

    if (!consume(p, TOK_RBRACE, "Expected '}' after block")) return NULL;
    return block;
}

static AstNode *parse_function(Parser *p) {
    int line = p->current.line;
    if (!consume(p, TOK_FUNC, "Expected 'func'")) return NULL;
    if (!consume(p, TOK_IDENT, "Expected function name")) return NULL;

    const char *name = ast_strndup(p->c, p->previous.start, p->previous.length);
    if (p->c->error) return NULL;

    if (!consume(p, TOK_LPAREN, "Expected '(' after function name")) return NULL;

    AstNode *params = NULL;
    AstNode *ptail = NULL;

    if (!check(p, TOK_RPAREN)) {
        do {
            if (!consume(p, TOK_IDENT, "Expected parameter name")) return NULL;
            AstNode *param = ast_new(p->c, AST_VARIABLE, p->previous.line);
            if (!param) return NULL;
            param->as.variable.name = ast_strndup(p->c, p->previous.start, p->previous.length);
            if (p->c->error) return NULL;
            if (ptail) ptail->next = param;
            else params = param;
            ptail = param;
        } while (match(p, TOK_COMMA));
    }

    if (!consume(p, TOK_RPAREN, "Expected ')' after parameters")) return NULL;

    AstNode *body = parse_block(p);
    if (!body) return NULL;

    AstNode *node = ast_new(p->c, AST_FUNCTION, line);
    if (node) {
        node->as.function.name = name;
        node->as.function.params = params;
        node->as.function.body = body;
    }
    return node;
}

static AstNode *parse_program(Parser *p) {
    AstNode *prog = ast_new(p->c, AST_PROGRAM, p->current.line);
    if (!prog) return NULL;
    AstNode *tail = NULL;

    while (!check(p, TOK_EOF)) {
        if (p->panic_mode) synchronize(p);
        AstNode *fn = parse_function(p);
        if (fn) {
            if (tail) tail->next = fn;
            else prog->as.program.first_func = fn;
            tail = fn;
        } else if (!p->panic_mode) {
            break;
        }
    }

    return prog;
}

static void parser_init(Parser *p, Compiler *c) {
    p->scanner.start = c->source;
    p->scanner.current = c->source;
    p->scanner.line = 1;
    p->scanner.c = c;
    p->c = c;
    p->had_error = false;
    p->panic_mode = false;
    p->current.type = TOK_EOF;
    p->current.start = "";
    p->current.length = 0;
    p->current.line = 1;
    p->previous = p->current;
    advance_parser(p);
}

/* ============================================================
 * Code generator
 * ============================================================ */

typedef struct {
    char *data;
    int len;
    int cap;
} AsmBuffer;

typedef struct {
    Compiler *c;
    AsmBuffer out;
    int label_count;

    int func_count;
    struct {
        const char *name;
        int index;
        int arity;
    } funcs[256];

    int arity;
    int local_count;
    struct {
        const char *name;
        int slot;
    } locals[256];
    bool declared[256];
} Gen;

static void asm_buf_init(AsmBuffer *b) {
    b->data = malloc(1024);
    b->cap = 1024;
    b->len = 0;
    if (b->data) b->data[0] = '\0';
}

static void asm_buf_free(AsmBuffer *b) {
    free(b->data);
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
}

static void emit(Gen *g, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(NULL, 0, fmt, args);
    va_end(args);

    if (n < 0) return;
    if (!g->out.data) return;

    while (g->out.len + n + 1 > g->out.cap) {
        g->out.cap *= 2;
        g->out.data = realloc(g->out.data, (size_t)g->out.cap);
        if (!g->out.data) {
            g->c->error = 1;
            snprintf(g->c->error_msg, sizeof(g->c->error_msg), "Out of memory");
            return;
        }
    }

    va_start(args, fmt);
    vsnprintf(g->out.data + g->out.len, (size_t)n + 1, fmt, args);
    va_end(args);
    g->out.len += n;
}

static void compile_error(Compiler *c, const char *fmt, ...) {
    if (c->error) return;
    c->error = 1;
    va_list args;
    va_start(args, fmt);
    vsnprintf(c->error_msg, sizeof(c->error_msg), fmt, args);
    va_end(args);
}

static int count_params(AstNode *params) {
    int count = 0;
    for (AstNode *p = params; p; p = p->next) count++;
    return count;
}

static int find_local(Gen *g, const char *name) {
    for (int i = 0; i < g->local_count; i++) {
        if (strcmp(g->locals[i].name, name) == 0) return g->locals[i].slot;
    }
    return -1;
}

static int find_function(Gen *g, const char *name) {
    for (int i = 0; i < g->func_count; i++) {
        if (strcmp(g->funcs[i].name, name) == 0) return g->funcs[i].index;
    }
    return -1;
}

static void add_param(Gen *g, const char *name) {
    if (g->local_count >= 256) {
        compile_error(g->c, "Too many locals in function");
        return;
    }
    g->locals[g->local_count].name = name;
    g->locals[g->local_count].slot = g->local_count;
    g->declared[g->local_count] = true;
    g->local_count++;
}

static int collect_vars(Gen *g, AstNode *node) {
    if (!node) return 0;

    switch (node->type) {
    case AST_BLOCK: {
        int added = 0;
        for (AstNode *stmt = node->as.block.first_stmt; stmt; stmt = stmt->next) {
            added += collect_vars(g, stmt);
        }
        return added;
    }
    case AST_VAR: {
        if (g->local_count >= 256) {
            compile_error(g->c, "Too many locals in function");
            return 0;
        }
        if (find_local(g, node->as.var.name) >= 0) {
            compile_error(g->c, "Duplicate variable '%s'", node->as.var.name);
            return 0;
        }
        g->locals[g->local_count].name = node->as.var.name;
        g->locals[g->local_count].slot = g->local_count;
        g->declared[g->local_count] = false;
        g->local_count++;
        return 1;
    }
    case AST_IF:
        return collect_vars(g, node->as.ifstmt.then_branch) +
               collect_vars(g, node->as.ifstmt.else_branch);
    case AST_WHILE:
        return collect_vars(g, node->as.whilestmt.body);
    default:
        return 0;
    }
}

static bool gen_expression(Gen *g, AstNode *node);
static bool gen_statement(Gen *g, AstNode *node);

static bool gen_print(Gen *g, AstNode *node, const char *builtin) {
    int argc = 0;
    for (AstNode *arg = node->as.call.args; arg; arg = arg->next) {
        if (!gen_expression(g, arg)) return false;
        argc++;
    }
    if (argc != 1) {
        compile_error(g->c, "'%s' expects 1 argument", builtin);
        return false;
    }
    if (strcmp(builtin, "println") == 0) {
        emit(g, "    println\n");
    } else {
        emit(g, "    print\n");
    }
    emit(g, "    null\n");
    return g->c->error == 0;
}

static bool gen_expression(Gen *g, AstNode *node) {
    if (!node) return true;

    switch (node->type) {
    case AST_LITERAL: {
        Value v = node->as.literal.value;
        switch (v.type) {
        case VAL_INT:
            if (v.as.integer >= -128 && v.as.integer <= 127) {
                emit(g, "    int8 %" PRId64 "\n", v.as.integer);
            } else {
                emit(g, "    int32 %" PRId64 "\n", v.as.integer);
            }
            break;
        case VAL_FLOAT:
            emit(g, "    float64 %g\n", v.as.floating);
            break;
        case VAL_BOOL:
            emit(g, "    %s\n", v.as.boolean ? "true" : "false");
            break;
        case VAL_NULL:
            emit(g, "    null\n");
            break;
        }
        return g->c->error == 0;
    }
    case AST_VARIABLE: {
        int slot = find_local(g, node->as.variable.name);
        if (slot < 0) {
            compile_error(g->c, "Undefined variable '%s'", node->as.variable.name);
            return false;
        }
        if (!g->declared[slot]) {
            compile_error(g->c, "Variable '%s' used before declaration", node->as.variable.name);
            return false;
        }
        emit(g, "    load_local %d\n", slot);
        return g->c->error == 0;
    }
    case AST_GROUPING:
        return gen_expression(g, node->as.grouping.expr);
    case AST_UNARY: {
        if (!gen_expression(g, node->as.unary.operand)) return false;
        switch (node->as.unary.op) {
        case TOK_MINUS: emit(g, "    neg\n"); break;
        case TOK_NOT:   emit(g, "    not\n"); break;
        default:
            compile_error(g->c, "Unknown unary operator");
            return false;
        }
        return g->c->error == 0;
    }
    case AST_BINARY: {
        int op = node->as.binary.op;
        /* Arithmetic: VM computes (top op second), so emit right then left
         * to get (left op right). Comparisons: VM computes (second op top),
         * so emit left then right. */
        if (op == TOK_PLUS || op == TOK_MINUS || op == TOK_STAR ||
            op == TOK_SLASH || op == TOK_PERCENT) {
            if (!gen_expression(g, node->as.binary.right)) return false;
            if (!gen_expression(g, node->as.binary.left)) return false;
        } else {
            if (!gen_expression(g, node->as.binary.left)) return false;
            if (!gen_expression(g, node->as.binary.right)) return false;
        }

        switch (op) {
        case TOK_PLUS:    emit(g, "    add\n"); break;
        case TOK_MINUS:   emit(g, "    sub\n"); break;
        case TOK_STAR:    emit(g, "    mul\n"); break;
        case TOK_SLASH:   emit(g, "    div\n"); break;
        case TOK_PERCENT: emit(g, "    mod\n"); break;
        case TOK_EQ:      emit(g, "    eq\n"); break;
        case TOK_NE:      emit(g, "    ne\n"); break;
        case TOK_LT:      emit(g, "    lt\n"); break;
        case TOK_GT:      emit(g, "    gt\n"); break;
        case TOK_LE:      emit(g, "    le\n"); break;
        case TOK_GE:      emit(g, "    ge\n"); break;
        default:
            compile_error(g->c, "Unknown binary operator");
            return false;
        }
        return g->c->error == 0;
    }
    case AST_CALL: {
        const char *name = node->as.call.name;
        if (strcmp(name, "print") == 0) return gen_print(g, node, "print");
        if (strcmp(name, "println") == 0) return gen_print(g, node, "println");

        int idx = find_function(g, name);
        if (idx < 0) {
            compile_error(g->c, "Undefined function '%s'", name);
            return false;
        }

        int argc = 0;
        for (AstNode *arg = node->as.call.args; arg; arg = arg->next) {
            if (!gen_expression(g, arg)) return false;
            argc++;
        }
        if (argc != g->funcs[idx].arity) {
            compile_error(g->c, "Function '%s' expects %d arguments, got %d",
                          name, g->funcs[idx].arity, argc);
            return false;
        }
        emit(g, "    call %d %d\n", idx, argc);
        return g->c->error == 0;
    }
    default:
        compile_error(g->c, "Invalid expression");
        return false;
    }
}

static bool gen_statement(Gen *g, AstNode *node) {
    if (!node) return true;

    switch (node->type) {
    case AST_BLOCK: {
        for (AstNode *stmt = node->as.block.first_stmt; stmt; stmt = stmt->next) {
            if (!gen_statement(g, stmt)) return false;
        }
        return true;
    }
    case AST_VAR: {
        int slot = find_local(g, node->as.var.name);
        if (slot < 0) {
            compile_error(g->c, "Internal error: var slot not found");
            return false;
        }
        g->declared[slot] = true;
        if (!gen_expression(g, node->as.var.init)) return false;
        emit(g, "    store_local %d\n", slot);
        return g->c->error == 0;
    }
    case AST_ASSIGN: {
        int slot = find_local(g, node->as.assign.name);
        if (slot < 0) {
            compile_error(g->c, "Undefined variable '%s'", node->as.assign.name);
            return false;
        }
        if (!g->declared[slot]) {
            compile_error(g->c, "Variable '%s' used before declaration", node->as.assign.name);
            return false;
        }
        if (!gen_expression(g, node->as.assign.value)) return false;
        emit(g, "    store_local %d\n", slot);
        return g->c->error == 0;
    }
    case AST_IF: {
        int n = g->label_count++;
        if (!gen_expression(g, node->as.ifstmt.cond)) return false;
        emit(g, "    jump_if_false @if_%d_else\n", n);
        if (!gen_statement(g, node->as.ifstmt.then_branch)) return false;
        emit(g, "    jump @if_%d_end\n", n);
        emit(g, "@if_%d_else:\n", n);
        if (node->as.ifstmt.else_branch) {
            if (!gen_statement(g, node->as.ifstmt.else_branch)) return false;
        }
        emit(g, "@if_%d_end:\n", n);
        return g->c->error == 0;
    }
    case AST_WHILE: {
        int n = g->label_count++;
        emit(g, "@while_%d_start:\n", n);
        if (!gen_expression(g, node->as.whilestmt.cond)) return false;
        emit(g, "    jump_if_false @while_%d_end\n", n);
        if (!gen_statement(g, node->as.whilestmt.body)) return false;
        emit(g, "    loop @while_%d_start\n", n);
        emit(g, "@while_%d_end:\n", n);
        return g->c->error == 0;
    }
    case AST_RETURN: {
        if (node->as.returnstmt.has_value) {
            if (!gen_expression(g, node->as.returnstmt.value)) return false;
        } else {
            emit(g, "    null\n");
        }
        emit(g, "    return\n");
        return g->c->error == 0;
    }
    case AST_EXPR_STMT: {
        if (!gen_expression(g, node->as.expr_stmt.expr)) return false;
        emit(g, "    pop\n");
        return g->c->error == 0;
    }
    default:
        compile_error(g->c, "Invalid statement");
        return false;
    }
}

static bool gen_function(Gen *g, AstNode *fn) {
    const char *name = fn->as.function.name;
    int arity = count_params(fn->as.function.params);

    g->arity = arity;
    g->local_count = 0;
    memset(g->declared, 0, sizeof(g->declared));

    for (AstNode *param = fn->as.function.params; param; param = param->next) {
        add_param(g, param->as.variable.name);
    }

    collect_vars(g, fn->as.function.body);
    if (g->c->error) return false;

    emit(g, ".func %s %d %d\n", name, arity, g->local_count);
    if (!gen_statement(g, fn->as.function.body)) return false;

    /* Implicit return. */
    emit(g, "    null\n");
    emit(g, "    return\n");
    return g->c->error == 0;
}

static bool gen_program(Gen *g, AstNode *program) {
    AstNode *main_fn = NULL;
    AstNode *others[256];
    int others_count = 0;

    for (AstNode *fn = program->as.program.first_func; fn; fn = fn->next) {
        const char *name = fn->as.function.name;
        if (strcmp(name, "main") == 0) {
            if (main_fn) {
                compile_error(g->c, "Duplicate function 'main'");
                return false;
            }
            main_fn = fn;
        } else {
            if (others_count >= 256) {
                compile_error(g->c, "Too many functions");
                return false;
            }
            others[others_count++] = fn;
        }
    }

    if (!main_fn) {
        compile_error(g->c, "No 'main' function defined");
        return false;
    }

    g->funcs[0].name = main_fn->as.function.name;
    g->funcs[0].index = 0;
    g->funcs[0].arity = count_params(main_fn->as.function.params);
    for (int i = 0; i < others_count; i++) {
        g->funcs[i + 1].name = others[i]->as.function.name;
        g->funcs[i + 1].index = i + 1;
        g->funcs[i + 1].arity = count_params(others[i]->as.function.params);
    }
    g->func_count = others_count + 1;

    /* Check for duplicate function names. */
    for (int i = 0; i < g->func_count; i++) {
        for (int j = i + 1; j < g->func_count; j++) {
            if (strcmp(g->funcs[i].name, g->funcs[j].name) == 0) {
                compile_error(g->c, "Duplicate function '%s'", g->funcs[i].name);
                return false;
            }
        }
    }

    if (!gen_function(g, main_fn)) return false;
    for (int i = 0; i < others_count; i++) {
        if (!gen_function(g, others[i])) return false;
    }
    return true;
}

/* ============================================================
 * Public API
 * ============================================================ */

void compiler_init(Compiler *c, const char *source) {
    memset(c, 0, sizeof(Compiler));
    c->source = strdup(source);
    asm_init(&c->assembler);
}

bool compiler_compile(Compiler *c) {
    Parser parser;
    parser_init(&parser, c);

    c->program = parse_program(&parser);
    if (!c->program || parser.had_error || c->error) {
        c->error = 1;
        if (c->error_msg[0] == '\0') {
            snprintf(c->error_msg, sizeof(c->error_msg), "Parse error");
        }
        return false;
    }

    bool has_main = false;
    for (AstNode *fn = c->program->as.program.first_func; fn; fn = fn->next) {
        if (strcmp(fn->as.function.name, "main") == 0) {
            if (has_main) {
                snprintf(c->error_msg, sizeof(c->error_msg), "Duplicate 'main' function");
                c->error = 1;
                return false;
            }
            has_main = true;
        }
    }
    if (!has_main) {
        snprintf(c->error_msg, sizeof(c->error_msg), "No 'main' function defined");
        c->error = 1;
        return false;
    }

    Gen gen;
    memset(&gen, 0, sizeof(Gen));
    gen.c = c;
    asm_buf_init(&gen.out);

    if (!gen_program(&gen, c->program)) {
        c->error = 1;
        asm_buf_free(&gen.out);
        if (c->error_msg[0] == '\0') {
            snprintf(c->error_msg, sizeof(c->error_msg), "Codegen error");
        }
        return false;
    }

    AsmError err = asm_parse(&c->assembler, gen.out.data);
    if (err != ASM_OK) {
        c->error = 1;
        snprintf(c->error_msg, sizeof(c->error_msg), "%s", c->assembler.error_msg);
        asm_buf_free(&gen.out);
        return false;
    }

    asm_buf_free(&gen.out);
    return true;
}

Chunk *compiler_get_main(Compiler *c) {
    return asm_get_main(&c->assembler);
}

const char *compiler_error_string(Compiler *c) {
    return c->error_msg[0] ? c->error_msg : "No error";
}

void compiler_free(Compiler *c) {
    free(c->source);
    c->source = NULL;
    asm_free(&c->assembler);

    AstAlloc *alloc = c->ast_allocs;
    while (alloc) {
        AstAlloc *next = alloc->next;
        free(alloc->ptr);
        free(alloc);
        alloc = next;
    }
    c->ast_allocs = NULL;
    c->program = NULL;
}
