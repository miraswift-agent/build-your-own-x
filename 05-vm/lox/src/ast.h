#ifndef LOX_AST_H
#define LOX_AST_H

#include "token.h"
#include "value.h"

typedef struct Expr Expr;
typedef struct Stmt Stmt;

/* ============================================================
 * Expressions
 * ============================================================ */

typedef enum {
    EXPR_ASSIGN,
    EXPR_BINARY,
    EXPR_CALL,
    EXPR_GET,
    EXPR_GROUPING,
    EXPR_LITERAL,
    EXPR_LOGICAL,
    EXPR_SET,
    EXPR_SUPER,
    EXPR_THIS,
    EXPR_UNARY,
    EXPR_VARIABLE
} ExprType;

struct Expr {
    ExprType type;
    union {
        struct { Token name; Expr* value; } assign;
        struct { Expr* left; Token op; Expr* right; } binary;
        struct { Expr* callee; Token paren; Expr** arguments; int argCount; } call;
        struct { Expr* object; Token name; } get;
        struct { Expr* expression; } grouping;
        struct { Value value; } literal;
        struct { Expr* left; Token op; Expr* right; } logical;
        struct { Expr* object; Token name; Expr* value; } set;
        struct { Token keyword; Token method; } super;
        struct { Token keyword; } thisExpr;
        struct { Token op; Expr* right; } unary;
        struct { Token name; } variable;
    } as;
};

Expr* newAssignExpr(Token name, Expr* value);
Expr* newBinaryExpr(Expr* left, Token op, Expr* right);
Expr* newCallExpr(Expr* callee, Token paren, Expr** arguments, int argCount);
Expr* newGetExpr(Expr* object, Token name);
Expr* newGroupingExpr(Expr* expression);
Expr* newLiteralExpr(Value value);
Expr* newLogicalExpr(Expr* left, Token op, Expr* right);
Expr* newSetExpr(Expr* object, Token name, Expr* value);
Expr* newSuperExpr(Token keyword, Token method);
Expr* newThisExpr(Token keyword);
Expr* newUnaryExpr(Token op, Expr* right);
Expr* newVariableExpr(Token name);

/* ============================================================
 * Statements
 * ============================================================ */

typedef enum {
    STMT_BLOCK,
    STMT_CLASS,
    STMT_EXPRESSION,
    STMT_FUNCTION,
    STMT_IF,
    STMT_PRINT,
    STMT_RETURN,
    STMT_VAR,
    STMT_WHILE
} StmtType;

struct Stmt {
    StmtType type;
    union {
        struct { Stmt** statements; int count; } block;
        struct { Token name; Expr* superclass; Stmt** methods; int methodCount; } classStmt;
        struct { Expr* expression; } expression;
        struct { Token name; Token* params; int arity; Stmt* body; } function;
        struct { Expr* condition; Stmt* thenBranch; Stmt* elseBranch; } ifStmt;
        struct { Expr* expression; } print;
        struct { Token keyword; Expr* value; } returnStmt;
        struct { Token name; Expr* initializer; } var;
        struct { Expr* condition; Stmt* body; } whileStmt;
    } as;
};

Stmt* newBlockStmt(Stmt** statements, int count);
Stmt* newClassStmt(Token name, Expr* superclass, Stmt** methods, int methodCount);
Stmt* newExpressionStmt(Expr* expression);
Stmt* newFunctionStmt(Token name, Token* params, int arity, Stmt* body);
Stmt* newIfStmt(Expr* condition, Stmt* thenBranch, Stmt* elseBranch);
Stmt* newPrintStmt(Expr* expression);
Stmt* newReturnStmt(Token keyword, Expr* value);
Stmt* newVarStmt(Token name, Expr* initializer);
Stmt* newWhileStmt(Expr* condition, Stmt* body);

#endif /* LOX_AST_H */
