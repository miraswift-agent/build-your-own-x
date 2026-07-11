#include "ast.h"

#include <string.h>

/* Expression constructors */

static Expr* newExpr(ExprType type) {
    Expr* expr = ALLOCATE(Expr);
    expr->type = type;
    return expr;
}

Expr* newAssignExpr(Token name, Expr* value) {
    Expr* expr = newExpr(EXPR_ASSIGN);
    expr->as.assign.name = name;
    expr->as.assign.value = value;
    return expr;
}

Expr* newBinaryExpr(Expr* left, Token op, Expr* right) {
    Expr* expr = newExpr(EXPR_BINARY);
    expr->as.binary.left = left;
    expr->as.binary.op = op;
    expr->as.binary.right = right;
    return expr;
}

Expr* newCallExpr(Expr* callee, Token paren, Expr** arguments, int argCount) {
    Expr* expr = newExpr(EXPR_CALL);
    expr->as.call.callee = callee;
    expr->as.call.paren = paren;
    expr->as.call.arguments = arguments;
    expr->as.call.argCount = argCount;
    return expr;
}

Expr* newGetExpr(Expr* object, Token name) {
    Expr* expr = newExpr(EXPR_GET);
    expr->as.get.object = object;
    expr->as.get.name = name;
    return expr;
}

Expr* newGroupingExpr(Expr* expression) {
    Expr* expr = newExpr(EXPR_GROUPING);
    expr->as.grouping.expression = expression;
    return expr;
}

Expr* newLiteralExpr(Value value) {
    Expr* expr = newExpr(EXPR_LITERAL);
    expr->as.literal.value = value;
    return expr;
}

Expr* newLogicalExpr(Expr* left, Token op, Expr* right) {
    Expr* expr = newExpr(EXPR_LOGICAL);
    expr->as.logical.left = left;
    expr->as.logical.op = op;
    expr->as.logical.right = right;
    return expr;
}

Expr* newSetExpr(Expr* object, Token name, Expr* value) {
    Expr* expr = newExpr(EXPR_SET);
    expr->as.set.object = object;
    expr->as.set.name = name;
    expr->as.set.value = value;
    return expr;
}

Expr* newSuperExpr(Token keyword, Token method) {
    Expr* expr = newExpr(EXPR_SUPER);
    expr->as.super.keyword = keyword;
    expr->as.super.method = method;
    return expr;
}

Expr* newThisExpr(Token keyword) {
    Expr* expr = newExpr(EXPR_THIS);
    expr->as.thisExpr.keyword = keyword;
    return expr;
}

Expr* newUnaryExpr(Token op, Expr* right) {
    Expr* expr = newExpr(EXPR_UNARY);
    expr->as.unary.op = op;
    expr->as.unary.right = right;
    return expr;
}

Expr* newVariableExpr(Token name) {
    Expr* expr = newExpr(EXPR_VARIABLE);
    expr->as.variable.name = name;
    return expr;
}

/* Statement constructors */

static Stmt* newStmt(StmtType type) {
    Stmt* stmt = ALLOCATE(Stmt);
    stmt->type = type;
    return stmt;
}

Stmt* newBlockStmt(Stmt** statements, int count) {
    Stmt* stmt = newStmt(STMT_BLOCK);
    stmt->as.block.statements = statements;
    stmt->as.block.count = count;
    return stmt;
}

Stmt* newClassStmt(Token name, Expr* superclass, Stmt** methods, int methodCount) {
    Stmt* stmt = newStmt(STMT_CLASS);
    stmt->as.classStmt.name = name;
    stmt->as.classStmt.superclass = superclass;
    stmt->as.classStmt.methods = methods;
    stmt->as.classStmt.methodCount = methodCount;
    return stmt;
}

Stmt* newExpressionStmt(Expr* expression) {
    Stmt* stmt = newStmt(STMT_EXPRESSION);
    stmt->as.expression.expression = expression;
    return stmt;
}

Stmt* newFunctionStmt(Token name, Token* params, int arity, Stmt* body) {
    Stmt* stmt = newStmt(STMT_FUNCTION);
    stmt->as.function.name = name;
    stmt->as.function.params = params;
    stmt->as.function.arity = arity;
    stmt->as.function.body = body;
    return stmt;
}

Stmt* newIfStmt(Expr* condition, Stmt* thenBranch, Stmt* elseBranch) {
    Stmt* stmt = newStmt(STMT_IF);
    stmt->as.ifStmt.condition = condition;
    stmt->as.ifStmt.thenBranch = thenBranch;
    stmt->as.ifStmt.elseBranch = elseBranch;
    return stmt;
}

Stmt* newPrintStmt(Expr* expression) {
    Stmt* stmt = newStmt(STMT_PRINT);
    stmt->as.print.expression = expression;
    return stmt;
}

Stmt* newReturnStmt(Token keyword, Expr* value) {
    Stmt* stmt = newStmt(STMT_RETURN);
    stmt->as.returnStmt.keyword = keyword;
    stmt->as.returnStmt.value = value;
    return stmt;
}

Stmt* newVarStmt(Token name, Expr* initializer) {
    Stmt* stmt = newStmt(STMT_VAR);
    stmt->as.var.name = name;
    stmt->as.var.initializer = initializer;
    return stmt;
}

Stmt* newWhileStmt(Expr* condition, Stmt* body) {
    Stmt* stmt = newStmt(STMT_WHILE);
    stmt->as.whileStmt.condition = condition;
    stmt->as.whileStmt.body = body;
    return stmt;
}
