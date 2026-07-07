#ifndef LOX_PARSER_H
#define LOX_PARSER_H

#include "scanner.h"
#include "ast.h"

typedef struct {
    Scanner scanner;
    Token current;
    Token previous;
    bool hadError;
    bool panicMode;
} Parser;

Stmt** parseSource(const char* source, int* outCount);

#endif /* LOX_PARSER_H */
