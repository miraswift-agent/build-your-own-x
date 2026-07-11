#include "parser.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lox.h"

#define MAX_ARGS 255

typedef struct {
    Stmt** items;
    int count;
    int capacity;
} StmtList;

typedef struct {
    Expr** items;
    int count;
    int capacity;
} ExprList;

typedef struct {
    Token* items;
    int count;
    int capacity;
} TokenList;

static void stmtListInit(StmtList* list) {
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

static void stmtListAdd(StmtList* list, Stmt* stmt) {
    if (list->count + 1 > list->capacity) {
        int oldCapacity = list->capacity;
        list->capacity = oldCapacity < 8 ? 8 : oldCapacity * 2;
        Stmt** grown = ALLOCATE_ARRAY(Stmt*, list->capacity);
        if (oldCapacity > 0) {
            memcpy(grown, list->items, sizeof(Stmt*) * list->count);
        }
        list->items = grown;
    }
    list->items[list->count++] = stmt;
}

static void exprListInit(ExprList* list) {
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

static void exprListAdd(ExprList* list, Expr* expr) {
    if (list->count + 1 > list->capacity) {
        int oldCapacity = list->capacity;
        list->capacity = oldCapacity < 8 ? 8 : oldCapacity * 2;
        Expr** grown = ALLOCATE_ARRAY(Expr*, list->capacity);
        if (oldCapacity > 0) {
            memcpy(grown, list->items, sizeof(Expr*) * list->count);
        }
        list->items = grown;
    }
    list->items[list->count++] = expr;
}

/* Forward declarations */
static Stmt* declaration(Parser* parser);
static Stmt* statement(Parser* parser);
static Expr* expression(Parser* parser);
static Expr* assignment(Parser* parser);
static Expr* orExpr(Parser* parser);
static Expr* andExpr(Parser* parser);
static Expr* equality(Parser* parser);
static Expr* comparison(Parser* parser);
static Expr* term(Parser* parser);
static Expr* factor(Parser* parser);
static Expr* unary(Parser* parser);
static Expr* call(Parser* parser);
static Expr* primary(Parser* parser);

/* Token helpers */

static void errorAt(Parser* parser, Token* token, const char* message) {
    if (parser->panicMode) return;
    parser->panicMode = true;
    loxError(token->line, "%s", message);
    parser->hadError = true;
}

static void error(Parser* parser, const char* message) {
    errorAt(parser, &parser->current, message);
}

static void advance(Parser* parser) {
    parser->previous = parser->current;

    for (;;) {
        parser->current = scannerScanToken(&parser->scanner);
        if (parser->current.type != TOKEN_EOF) break;

        if (strcmp(parser->current.start, "Unterminated string.") == 0) {
            error(parser, parser->current.start);
        }
        break;
    }
}

static bool check(Parser* parser, TokenType type) {
    return parser->current.type == type;
}

static bool match(Parser* parser, TokenType type) {
    if (!check(parser, type)) return false;
    advance(parser);
    return true;
}

static Token consume(Parser* parser, TokenType type, const char* message) {
    if (parser->current.type == type) {
        advance(parser);
        return parser->previous;
    }
    error(parser, message);
    return parser->current;
}

static void synchronize(Parser* parser) {
    parser->panicMode = false;

    while (parser->current.type != TOKEN_EOF) {
        if (parser->previous.type == TOKEN_SEMICOLON) return;
        switch (parser->current.type) {
            case TOKEN_CLASS:
            case TOKEN_FUN:
            case TOKEN_VAR:
            case TOKEN_FOR:
            case TOKEN_IF:
            case TOKEN_WHILE:
            case TOKEN_PRINT:
            case TOKEN_RETURN:
                return;
            default:
                ; /* nothing */
        }
        advance(parser);
    }
}

static char* processString(Parser* parser, const char* source, size_t length,
                           size_t* outLength) {
    char* result = ALLOCATE_ARRAY(char, length + 1);
    size_t j = 0;
    for (size_t i = 0; i < length; i++) {
        if (source[i] == '\\' && i + 1 < length) {
            switch (source[i + 1]) {
                case 'n': result[j++] = '\n'; i++; break;
                case 't': result[j++] = '\t'; i++; break;
                case 'r': result[j++] = '\r'; i++; break;
                case '"': result[j++] = '"'; i++; break;
                case '\\': result[j++] = '\\'; i++; break;
                default:
                    loxError(parser->previous.line,
                             "Invalid escape sequence '\\%c'.", source[i + 1]);
                    result[j++] = source[i + 1];
                    i++;
                    break;
            }
        } else {
            result[j++] = source[i];
        }
    }
    result[j] = '\0';
    *outLength = j;
    return result;
}

/* Expression parsing */

static Expr* expression(Parser* parser) {
    return assignment(parser);
}

static Expr* assignment(Parser* parser) {
    Expr* expr = orExpr(parser);

    if (match(parser, TOKEN_EQUAL)) {
        Token equals = parser->previous;
        Expr* value = assignment(parser);

        if (expr->type == EXPR_VARIABLE) {
            return newAssignExpr(expr->as.variable.name, value);
        } else if (expr->type == EXPR_GET) {
            return newSetExpr(expr->as.get.object, expr->as.get.name, value);
        }

        errorAt(parser, &equals, "Invalid assignment target.");
    }

    return expr;
}

static Expr* orExpr(Parser* parser) {
    Expr* expr = andExpr(parser);

    while (match(parser, TOKEN_OR)) {
        Token op = parser->previous;
        Expr* right = andExpr(parser);
        expr = newLogicalExpr(expr, op, right);
    }

    return expr;
}

static Expr* andExpr(Parser* parser) {
    Expr* expr = equality(parser);

    while (match(parser, TOKEN_AND)) {
        Token op = parser->previous;
        Expr* right = equality(parser);
        expr = newLogicalExpr(expr, op, right);
    }

    return expr;
}

static Expr* equality(Parser* parser) {
    Expr* expr = comparison(parser);

    while (match(parser, TOKEN_BANG_EQUAL) || match(parser, TOKEN_EQUAL_EQUAL)) {
        Token op = parser->previous;
        Expr* right = comparison(parser);
        expr = newBinaryExpr(expr, op, right);
    }

    return expr;
}

static Expr* comparison(Parser* parser) {
    Expr* expr = term(parser);

    while (match(parser, TOKEN_GREATER) || match(parser, TOKEN_GREATER_EQUAL) ||
           match(parser, TOKEN_LESS) || match(parser, TOKEN_LESS_EQUAL)) {
        Token op = parser->previous;
        Expr* right = term(parser);
        expr = newBinaryExpr(expr, op, right);
    }

    return expr;
}

static Expr* term(Parser* parser) {
    Expr* expr = factor(parser);

    while (match(parser, TOKEN_MINUS) || match(parser, TOKEN_PLUS)) {
        Token op = parser->previous;
        Expr* right = factor(parser);
        expr = newBinaryExpr(expr, op, right);
    }

    return expr;
}

static Expr* factor(Parser* parser) {
    Expr* expr = unary(parser);

    while (match(parser, TOKEN_SLASH) || match(parser, TOKEN_STAR)) {
        Token op = parser->previous;
        Expr* right = unary(parser);
        expr = newBinaryExpr(expr, op, right);
    }

    return expr;
}

static Expr* unary(Parser* parser) {
    if (match(parser, TOKEN_BANG) || match(parser, TOKEN_MINUS)) {
        Token op = parser->previous;
        Expr* right = unary(parser);
        return newUnaryExpr(op, right);
    }

    return call(parser);
}

static Expr* finishCall(Parser* parser, Expr* callee) {
    ExprList args;
    exprListInit(&args);

    if (!check(parser, TOKEN_RIGHT_PAREN)) {
        do {
            if (args.count >= MAX_ARGS) {
                errorAt(parser, &parser->current,
                        "Can't have more than 255 arguments.");
            }
            exprListAdd(&args, expression(parser));
        } while (match(parser, TOKEN_COMMA));
    }

    Token paren = consume(parser, TOKEN_RIGHT_PAREN, "Expect ')' after arguments.");

    Expr** arguments = NULL;
    if (args.count > 0) {
        arguments = ALLOCATE_ARRAY(Expr*, args.count);
        memcpy(arguments, args.items, sizeof(Expr*) * args.count);
    }

    return newCallExpr(callee, paren, arguments, args.count);
}

static Expr* call(Parser* parser) {
    Expr* expr = primary(parser);

    for (;;) {
        if (match(parser, TOKEN_LEFT_PAREN)) {
            expr = finishCall(parser, expr);
        } else if (match(parser, TOKEN_DOT)) {
            Token name = consume(parser, TOKEN_IDENTIFIER,
                                 "Expect property name after '.'.");
            expr = newGetExpr(expr, name);
        } else {
            break;
        }
    }

    return expr;
}

static Expr* primary(Parser* parser) {
    if (match(parser, TOKEN_FALSE)) {
        return newLiteralExpr(boolValue(false));
    }
    if (match(parser, TOKEN_TRUE)) {
        return newLiteralExpr(boolValue(true));
    }
    if (match(parser, TOKEN_NIL)) {
        return newLiteralExpr(nilValue());
    }

    if (match(parser, TOKEN_NUMBER)) {
        return newLiteralExpr(numberValue(parser->previous.literalNumber));
    }

    if (match(parser, TOKEN_STRING)) {
        const char* start = parser->previous.start + 1;
        size_t length = parser->previous.length - 2;
        size_t processedLength;
        char* processed = processString(parser, start, length, &processedLength);
        return newLiteralExpr(stringValue(processed, processedLength));
    }

    if (match(parser, TOKEN_IDENTIFIER)) {
        return newVariableExpr(parser->previous);
    }

    if (match(parser, TOKEN_THIS)) {
        return newThisExpr(parser->previous);
    }

    if (match(parser, TOKEN_LEFT_PAREN)) {
        Expr* expr = expression(parser);
        consume(parser, TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
        return newGroupingExpr(expr);
    }

    if (match(parser, TOKEN_SUPER)) {
        Token keyword = parser->previous;
        consume(parser, TOKEN_DOT, "Expect '.' after 'super'.");
        Token method = consume(parser, TOKEN_IDENTIFIER,
                               "Expect superclass method name.");
        return newSuperExpr(keyword, method);
    }

    error(parser, "Expect expression.");
    return newLiteralExpr(nilValue());
}

/* Statement parsing */

static Stmt* expressionStatement(Parser* parser) {
    Expr* expr = expression(parser);
    consume(parser, TOKEN_SEMICOLON, "Expect ';' after expression.");
    return newExpressionStmt(expr);
}

static Stmt* printStatement(Parser* parser) {
    Expr* value = expression(parser);
    consume(parser, TOKEN_SEMICOLON, "Expect ';' after value.");
    return newPrintStmt(value);
}

static Stmt* returnStatement(Parser* parser) {
    Token keyword = parser->previous;
    Expr* value = NULL;
    if (!check(parser, TOKEN_SEMICOLON)) {
        value = expression(parser);
    }
    consume(parser, TOKEN_SEMICOLON, "Expect ';' after return value.");
    return newReturnStmt(keyword, value);
}

static Stmt* blockStatement(Parser* parser) {
    StmtList statements;
    stmtListInit(&statements);

    while (!check(parser, TOKEN_RIGHT_BRACE) && !check(parser, TOKEN_EOF)) {
        stmtListAdd(&statements, declaration(parser));
    }

    consume(parser, TOKEN_RIGHT_BRACE, "Expect '}' after block.");

    Stmt** blockStmts = NULL;
    if (statements.count > 0) {
        blockStmts = ALLOCATE_ARRAY(Stmt*, statements.count);
        memcpy(blockStmts, statements.items, sizeof(Stmt*) * statements.count);
    }
    return newBlockStmt(blockStmts, statements.count);
}

static Stmt* ifStatement(Parser* parser) {
    consume(parser, TOKEN_LEFT_PAREN, "Expect '(' after 'if'.");
    Expr* condition = expression(parser);
    consume(parser, TOKEN_RIGHT_PAREN, "Expect ')' after if condition.");

    Stmt* thenBranch = statement(parser);
    Stmt* elseBranch = NULL;
    if (match(parser, TOKEN_ELSE)) {
        elseBranch = statement(parser);
    }

    return newIfStmt(condition, thenBranch, elseBranch);
}

static Stmt* whileStatement(Parser* parser) {
    consume(parser, TOKEN_LEFT_PAREN, "Expect '(' after 'while'.");
    Expr* condition = expression(parser);
    consume(parser, TOKEN_RIGHT_PAREN, "Expect ')' after condition.");
    Stmt* body = statement(parser);
    return newWhileStmt(condition, body);
}

static Stmt* forStatement(Parser* parser) {
    consume(parser, TOKEN_LEFT_PAREN, "Expect '(' after 'for'.");

    Stmt* initializer = NULL;
    if (match(parser, TOKEN_SEMICOLON)) {
        initializer = NULL;
    } else if (match(parser, TOKEN_VAR)) {
        Token name = consume(parser, TOKEN_IDENTIFIER, "Expect variable name.");
        Expr* initExpr = NULL;
        if (match(parser, TOKEN_EQUAL)) {
            initExpr = expression(parser);
        }
        consume(parser, TOKEN_SEMICOLON, "Expect ';' after loop variable.");
        initializer = newVarStmt(name, initExpr);
    } else {
        Expr* expr = expression(parser);
        consume(parser, TOKEN_SEMICOLON, "Expect ';' after loop initializer.");
        initializer = newExpressionStmt(expr);
    }

    Expr* condition = NULL;
    if (!check(parser, TOKEN_SEMICOLON)) {
        condition = expression(parser);
    }
    consume(parser, TOKEN_SEMICOLON, "Expect ';' after loop condition.");

    Expr* increment = NULL;
    if (!check(parser, TOKEN_RIGHT_PAREN)) {
        increment = expression(parser);
    }
    consume(parser, TOKEN_RIGHT_PAREN, "Expect ')' after for clauses.");

    Stmt* body = statement(parser);

    /* Desugar increment into body. */
    if (increment != NULL) {
        StmtList bodyList;
        stmtListInit(&bodyList);
        stmtListAdd(&bodyList, body);
        stmtListAdd(&bodyList, newExpressionStmt(increment));
        Stmt** blockStmts = ALLOCATE_ARRAY(Stmt*, bodyList.count);
        memcpy(blockStmts, bodyList.items, sizeof(Stmt*) * bodyList.count);
        body = newBlockStmt(blockStmts, bodyList.count);
    }

    /* Desugar condition; default to true. */
    if (condition == NULL) {
        condition = newLiteralExpr(boolValue(true));
    }
    body = newWhileStmt(condition, body);

    /* Desugar initializer into outer block. */
    if (initializer != NULL) {
        StmtList outerList;
        stmtListInit(&outerList);
        stmtListAdd(&outerList, initializer);
        stmtListAdd(&outerList, body);
        Stmt** blockStmts = ALLOCATE_ARRAY(Stmt*, outerList.count);
        memcpy(blockStmts, outerList.items, sizeof(Stmt*) * outerList.count);
        body = newBlockStmt(blockStmts, outerList.count);
    }

    return body;
}

static Stmt* statement(Parser* parser) {
    if (match(parser, TOKEN_PRINT)) return printStatement(parser);
    if (match(parser, TOKEN_RETURN)) return returnStatement(parser);
    if (match(parser, TOKEN_FOR)) return forStatement(parser);
    if (match(parser, TOKEN_IF)) return ifStatement(parser);
    if (match(parser, TOKEN_WHILE)) return whileStatement(parser);
    if (match(parser, TOKEN_LEFT_BRACE)) return blockStatement(parser);
    return expressionStatement(parser);
}

static Stmt* functionDeclaration(Parser* parser, const char* kind) {
    (void)kind;
    Token name = consume(parser, TOKEN_IDENTIFIER, "Expect function name.");
    consume(parser, TOKEN_LEFT_PAREN, "Expect '(' after function name.");

    Token* params = NULL;
    int arity = 0;
    if (!check(parser, TOKEN_RIGHT_PAREN)) {
        TokenList paramList;
        paramList.items = NULL;
        paramList.count = 0;
        paramList.capacity = 0;

        do {
            if (arity >= MAX_ARGS) {
                errorAt(parser, &parser->current,
                        "Can't have more than 255 parameters.");
            }

            Token param = consume(parser, TOKEN_IDENTIFIER, "Expect parameter name.");

            if (paramList.count + 1 > paramList.capacity) {
                int oldCapacity = paramList.capacity;
                paramList.capacity = oldCapacity < 8 ? 8 : oldCapacity * 2;
                Token* grown = ALLOCATE_ARRAY(Token, paramList.capacity);
                if (oldCapacity > 0) {
                    memcpy(grown, paramList.items, sizeof(Token) * paramList.count);
                }
                paramList.items = grown;
            }
            paramList.items[paramList.count++] = param;
            arity++;
        } while (match(parser, TOKEN_COMMA));

        if (arity > 0) {
            params = ALLOCATE_ARRAY(Token, arity);
            memcpy(params, paramList.items, sizeof(Token) * arity);
        }
    }

    consume(parser, TOKEN_RIGHT_PAREN, "Expect ')' after parameters.");
    consume(parser, TOKEN_LEFT_BRACE, "Expect '{' before function body.");
    Stmt* body = blockStatement(parser);

    return newFunctionStmt(name, params, arity, body);
}

static Stmt* classDeclaration(Parser* parser) {
    Token name = consume(parser, TOKEN_IDENTIFIER, "Expect class name.");

    Expr* superclass = NULL;
    if (match(parser, TOKEN_LESS)) {
        consume(parser, TOKEN_IDENTIFIER, "Expect superclass name.");
        superclass = newVariableExpr(parser->previous);
    }

    consume(parser, TOKEN_LEFT_BRACE, "Expect '{' before class body.");

    StmtList methods;
    stmtListInit(&methods);
    while (!check(parser, TOKEN_RIGHT_BRACE) && !check(parser, TOKEN_EOF)) {
        stmtListAdd(&methods, functionDeclaration(parser, "method"));
    }

    consume(parser, TOKEN_RIGHT_BRACE, "Expect '}' after class body.");

    Stmt** methodArray = NULL;
    if (methods.count > 0) {
        methodArray = ALLOCATE_ARRAY(Stmt*, methods.count);
        memcpy(methodArray, methods.items, sizeof(Stmt*) * methods.count);
    }

    return newClassStmt(name, superclass, methodArray, methods.count);
}

static Stmt* varDeclaration(Parser* parser) {
    Token name = consume(parser, TOKEN_IDENTIFIER, "Expect variable name.");

    Expr* initializer = NULL;
    if (match(parser, TOKEN_EQUAL)) {
        initializer = expression(parser);
    }

    consume(parser, TOKEN_SEMICOLON, "Expect ';' after variable declaration.");
    return newVarStmt(name, initializer);
}

static Stmt* declaration(Parser* parser) {
    if (match(parser, TOKEN_CLASS)) return classDeclaration(parser);
    if (match(parser, TOKEN_FUN)) return functionDeclaration(parser, "function");
    if (match(parser, TOKEN_VAR)) return varDeclaration(parser);

    return statement(parser);
}

Stmt** parseSource(const char* source, int* outCount) {
    Parser parser;
    parser.scanner.start = source;
    parser.scanner.current = source;
    parser.scanner.line = 1;
    parser.hadError = false;
    parser.panicMode = false;
    parser.current.type = TOKEN_EOF;
    parser.current.start = "";
    parser.current.length = 0;
    parser.current.line = 1;
    parser.previous = parser.current;

    advance(&parser);

    StmtList list;
    stmtListInit(&list);

    while (!check(&parser, TOKEN_EOF)) {
        stmtListAdd(&list, declaration(&parser));
        if (parser.panicMode) synchronize(&parser);
    }

    if (list.count == 0) {
        *outCount = 0;
        return NULL;
    }

    Stmt** statements = ALLOCATE_ARRAY(Stmt*, list.count);
    memcpy(statements, list.items, sizeof(Stmt*) * list.count);
    *outCount = list.count;

    if (parser.hadError) {
        g_lox.hadParseError = true;
    }

    return statements;
}
