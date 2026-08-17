#include "interpreter.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "lox.h"
#include "ast.h"

Interpreter g_interpreter;

static void execute(Stmt* stmt);
static Value evaluate(Expr* expr);

static char* tokenText(Token token) {
    char* text = loxAllocate(token.length + 1);
    memcpy(text, token.start, token.length);
    text[token.length] = '\0';
    return text;
}

/* ============================================================
 * Native functions
 * ============================================================ */

static Value clockNative(int argCount, Value* args) {
    (void)argCount;
    (void)args;
    return numberValue((double)clock() / CLOCKS_PER_SEC);
}

/* ============================================================
 * Environment helpers
 * ============================================================ */

static void define(const char* name, Value value) {
    envDefine(g_interpreter.environment, name, value);
}

static bool lookupVariable(const char* name, Value* out) {
    return envGet(g_interpreter.environment, name, out);
}

static bool assignVariable(const char* name, Value value) {
    return envAssign(g_interpreter.environment, name, value);
}

/* ============================================================
 * Evaluation
 * ============================================================ */

static Value evaluate(Expr* expr) {
    switch (expr->type) {
        case EXPR_LITERAL:
            return expr->as.literal.value;

        case EXPR_GROUPING:
            return evaluate(expr->as.grouping.expression);

        case EXPR_VARIABLE: {
            Value value;
            char* name = tokenText(expr->as.variable.name);
            g_lox.currentLine = expr->as.variable.name.line;
            if (!lookupVariable(name, &value)) {
                loxRuntimeError("Undefined variable '%s'.", name);
            }
            return value;
        }

        case EXPR_ASSIGN: {
            Value value = evaluate(expr->as.assign.value);
            char* name = tokenText(expr->as.assign.name);
            g_lox.currentLine = expr->as.assign.name.line;
            if (!assignVariable(name, value)) {
                loxRuntimeError("Undefined variable '%s'.", name);
            }
            return value;
        }

        case EXPR_UNARY: {
            Value right = evaluate(expr->as.unary.right);
            g_lox.currentLine = expr->as.unary.op.line;
            switch (expr->as.unary.op.type) {
                case TOKEN_MINUS:
                    if (!isNumber(right)) {
                        loxRuntimeError("Operand must be a number.");
                    }
                    return numberValue(-asNumber(right));
                case TOKEN_BANG:
                    return boolValue(!isTruthy(right));
                default:
                    loxRuntimeError("Unknown unary operator.");
                    return nilValue();
            }
        }

        case EXPR_BINARY: {
            Value left = evaluate(expr->as.binary.left);
            Value right = evaluate(expr->as.binary.right);
            g_lox.currentLine = expr->as.binary.op.line;

            switch (expr->as.binary.op.type) {
                case TOKEN_BANG_EQUAL:
                    return boolValue(!valuesEqual(left, right));
                case TOKEN_EQUAL_EQUAL:
                    return boolValue(valuesEqual(left, right));
                case TOKEN_GREATER:
                case TOKEN_GREATER_EQUAL:
                case TOKEN_LESS:
                case TOKEN_LESS_EQUAL: {
                    if (!isNumber(left) || !isNumber(right)) {
                        loxRuntimeError("Operands must be numbers.");
                    }
                    double a = asNumber(left);
                    double b = asNumber(right);
                    switch (expr->as.binary.op.type) {
                        case TOKEN_GREATER:       return boolValue(a > b);
                        case TOKEN_GREATER_EQUAL: return boolValue(a >= b);
                        case TOKEN_LESS:          return boolValue(a < b);
                        case TOKEN_LESS_EQUAL:    return boolValue(a <= b);
                        default: break;
                    }
                    break;
                }
                case TOKEN_MINUS:
                    if (!isNumber(left) || !isNumber(right)) {
                        loxRuntimeError("Operands must be numbers.");
                    }
                    return numberValue(asNumber(left) - asNumber(right));
                case TOKEN_PLUS:
                    if (isNumber(left) && isNumber(right)) {
                        return numberValue(asNumber(left) + asNumber(right));
                    }
                    if (isString(left) && isString(right)) {
                        ObjString* a = asString(left);
                        ObjString* b = asString(right);
                        size_t length = a->length + b->length;
                        char* chars = loxAllocate(length + 1);
                        memcpy(chars, a->chars, a->length);
                        memcpy(chars + a->length, b->chars, b->length);
                        chars[length] = '\0';
                        return objValue((Obj*)newString(chars, length));
                    }
                    loxRuntimeError("Operands must be two numbers or two strings.");
                    break;
                case TOKEN_SLASH:
                    if (!isNumber(left) || !isNumber(right)) {
                        loxRuntimeError("Operands must be numbers.");
                    }
                    return numberValue(asNumber(left) / asNumber(right));
                case TOKEN_STAR:
                    if (!isNumber(left) || !isNumber(right)) {
                        loxRuntimeError("Operands must be numbers.");
                    }
                    return numberValue(asNumber(left) * asNumber(right));
                default:
                    loxRuntimeError("Unknown binary operator.");
            }
            return nilValue();
        }

        case EXPR_LOGICAL: {
            Value left = evaluate(expr->as.logical.left);
            g_lox.currentLine = expr->as.logical.op.line;
            if (expr->as.logical.op.type == TOKEN_OR) {
                if (isTruthy(left)) return left;
            } else { /* TOKEN_AND */
                if (!isTruthy(left)) return left;
            }
            return evaluate(expr->as.logical.right);
        }

        case EXPR_CALL: {
            Value callee = evaluate(expr->as.call.callee);
            g_lox.currentLine = expr->as.call.paren.line;

            Value args[255];
            if (expr->as.call.argCount > 255) {
                loxRuntimeError("Can't have more than 255 arguments.");
            }
            for (int i = 0; i < expr->as.call.argCount; i++) {
                args[i] = evaluate(expr->as.call.arguments[i]);
            }

            if (!isCallable(callee)) {
                loxRuntimeError("Can only call functions and classes.");
            }

            if (isNative(callee)) {
                ObjNative* native = asNative(callee);
                if (expr->as.call.argCount != native->arity) {
                    loxRuntimeError("Expected %d arguments but got %d.",
                                  native->arity, expr->as.call.argCount);
                }
                return native->function(expr->as.call.argCount, args);
            }

            if (isFunction(callee)) {
                ObjFunction* function = asFunction(callee);
                if (expr->as.call.argCount != function->arity) {
                    loxRuntimeError("Expected %d arguments but got %d.",
                                  function->arity, expr->as.call.argCount);
                }
                return callFunction(function, expr->as.call.argCount, args);
            }

            if (isBoundMethod(callee)) {
                ObjBoundMethod* bound = asBoundMethod(callee);
                ObjFunction* method = bound->method;
                if (expr->as.call.argCount != method->arity) {
                    loxRuntimeError("Expected %d arguments but got %d.",
                                  method->arity, expr->as.call.argCount);
                }
                /* Prepend receiver as 'this'. */
                Value callArgs[256];
                callArgs[0] = objValue((Obj*)bound->receiver);
                for (int i = 0; i < expr->as.call.argCount; i++) {
                    callArgs[i + 1] = args[i];
                }
                return callFunction(method, expr->as.call.argCount + 1, callArgs);
            }

            if (isClass(callee)) {
                ObjClass* klass = asClass(callee);
                ObjInstance* instance = newInstance(klass);

                Value initializer;
                if (envGet(klass->methods, "init", &initializer)) {
                    ObjFunction* init = asFunction(initializer);
                    if (expr->as.call.argCount != init->arity) {
                        loxRuntimeError("Expected %d arguments but got %d.",
                                      init->arity, expr->as.call.argCount);
                    }
                    Value callArgs[256];
                    callArgs[0] = objValue((Obj*)instance);
                    for (int i = 0; i < expr->as.call.argCount; i++) {
                        callArgs[i + 1] = args[i];
                    }
                    callFunction(init, expr->as.call.argCount + 1, callArgs);
                } else if (expr->as.call.argCount != 0) {
                    loxRuntimeError("Expected 0 arguments but got %d.",
                                  expr->as.call.argCount);
                }

                return objValue((Obj*)instance);
            }

            loxRuntimeError("Can only call functions and classes.");
            return nilValue();
        }

        case EXPR_GET: {
            Value object = evaluate(expr->as.get.object);
            g_lox.currentLine = expr->as.get.name.line;
            if (!isInstance(object)) {
                loxRuntimeError("Only instances have properties.");
            }
            ObjInstance* instance = asInstance(object);
            char* name = tokenText(expr->as.get.name);

            Value value;
            if (envGet(instance->fields, name, &value)) {
                return value;
            }

            if (envGet(instance->klass->methods, name, &value)) {
                ObjFunction* method = asFunction(value);
                return objValue((Obj*)newBoundMethod(instance, method));
            }

            loxRuntimeError("Undefined property '%s'.", name);
            return nilValue();
        }

        case EXPR_SET: {
            Value object = evaluate(expr->as.set.object);
            g_lox.currentLine = expr->as.set.name.line;
            if (!isInstance(object)) {
                loxRuntimeError("Only instances have fields.");
            }
            Value value = evaluate(expr->as.set.value);
            ObjInstance* instance = asInstance(object);
            char* name = tokenText(expr->as.set.name);
            envDefine(instance->fields, name, value);
            return value;
        }

        case EXPR_THIS: {
            Value value;
            char* name = tokenText(expr->as.thisExpr.keyword);
            g_lox.currentLine = expr->as.thisExpr.keyword.line;
            if (!lookupVariable(name, &value)) {
                loxRuntimeError("Undefined variable '%s'.", name);
            }
            return value;
        }

        case EXPR_SUPER: {
            char* methodName = tokenText(expr->as.super.method);
            g_lox.currentLine = expr->as.super.keyword.line;

            Value superValue;
            if (!lookupVariable("super", &superValue)) {
                loxRuntimeError("Can't use 'super' outside of a class.");
            }
            ObjClass* superclass = asClass(superValue);

            Value thisValue;
            if (!lookupVariable("this", &thisValue)) {
                loxRuntimeError("Can't use 'super' outside of a class method.");
            }
            ObjInstance* instance = asInstance(thisValue);

            Value methodValue;
            if (!envGet(superclass->methods, methodName, &methodValue)) {
                loxRuntimeError("Undefined property '%s'.", methodName);
            }
            ObjFunction* method = asFunction(methodValue);

            return objValue((Obj*)newBoundMethod(instance, method));
        }
    }

    loxRuntimeError("Unknown expression type.");
    return nilValue();
}

/* ============================================================
 * Statement execution
 * ============================================================ */

static void execute(Stmt* stmt) {
    if (g_interpreter.returning) return;

    switch (stmt->type) {
        case STMT_EXPRESSION: {
            evaluate(stmt->as.expression.expression);
            break;
        }

        case STMT_PRINT: {
            Value value = evaluate(stmt->as.print.expression);
            valuePrint(value);
            printf("\n");
            break;
        }

        case STMT_VAR: {
            Value value = nilValue();
            if (stmt->as.var.initializer != NULL) {
                value = evaluate(stmt->as.var.initializer);
            }
            char* name = tokenText(stmt->as.var.name);
            define(name, value);
            break;
        }

        case STMT_BLOCK: {
            Environment* env = newEnvironment(g_interpreter.environment);
            interpreterExecuteBlock(stmt->as.block.statements,
                                    stmt->as.block.count, env);
            break;
        }

        case STMT_IF: {
            Value condition = evaluate(stmt->as.ifStmt.condition);
            if (isTruthy(condition)) {
                execute(stmt->as.ifStmt.thenBranch);
            } else if (stmt->as.ifStmt.elseBranch != NULL) {
                execute(stmt->as.ifStmt.elseBranch);
            }
            break;
        }

        case STMT_WHILE: {
            while (!g_interpreter.returning &&
                   isTruthy(evaluate(stmt->as.whileStmt.condition))) {
                execute(stmt->as.whileStmt.body);
            }
            break;
        }

        case STMT_RETURN: {
            Value value = nilValue();
            if (stmt->as.returnStmt.value != NULL) {
                value = evaluate(stmt->as.returnStmt.value);
            }
            g_interpreter.returnValue = value;
            g_interpreter.returning = true;
            break;
        }

        case STMT_FUNCTION: {
            char* name = tokenText(stmt->as.function.name);
            ObjFunction* function = newFunction(name);
            function->arity = stmt->as.function.arity;
            function->params = stmt->as.function.params;
            function->body = stmt->as.function.body;
            function->closure = g_interpreter.environment;
            define(name, objValue((Obj*)function));
            break;
        }

        case STMT_CLASS: {
            char* name = tokenText(stmt->as.classStmt.name);
            Value superclassValue = nilValue();
            ObjClass* superclass = NULL;

            if (stmt->as.classStmt.superclass != NULL) {
                superclassValue = evaluate(stmt->as.classStmt.superclass);
                if (!isClass(superclassValue)) {
                    loxRuntimeError("Superclass must be a class.");
                }
                superclass = asClass(superclassValue);
            }

            define(name, nilValue());

            Environment* classEnv = newEnvironment(g_interpreter.environment);
            if (superclass != NULL) {
                envDefine(classEnv, "super", superclassValue);
            }

            for (int i = 0; i < stmt->as.classStmt.methodCount; i++) {
                Stmt* method = stmt->as.classStmt.methods[i];
                char* methodName = tokenText(method->as.function.name);

                ObjFunction* function = newFunction(methodName);
                function->arity = method->as.function.arity;
                function->params = method->as.function.params;
                function->body = method->as.function.body;
                function->closure = classEnv;

                envDefine(classEnv, methodName, objValue((Obj*)function));
            }

            ObjClass* klass = newClass(name, superclass);
            klass->methods = classEnv;

            /* Update the class binding in the current environment. */
            assignVariable(name, objValue((Obj*)klass));
            break;
        }
    }
}

void interpreterExecuteBlock(Stmt** statements, int count, Environment* env) {
    Environment* previous = g_interpreter.environment;
    g_interpreter.environment = env;

    for (int i = 0; i < count && !g_interpreter.returning; i++) {
        execute(statements[i]);
    }

    g_interpreter.environment = previous;
}

Value callFunction(ObjFunction* function, int argCount, Value* args) {
    Environment* env = newEnvironment(function->closure);

    int paramOffset = 0;
    if (argCount == function->arity + 1) {
        /* Method call with implicit receiver. */
        envDefine(env, "this", args[0]);
        paramOffset = 1;
    } else if (argCount != function->arity) {
        loxRuntimeError("Expected %d arguments but got %d.",
                        function->arity, argCount);
    }

    for (int i = 0; i < function->arity; i++) {
        char* paramName = tokenText(function->params[i]);
        envDefine(env, paramName, args[paramOffset + i]);
    }

    bool savedReturning = g_interpreter.returning;
    Value savedReturnValue = g_interpreter.returnValue;
    g_interpreter.returning = false;

    interpreterExecuteBlock(function->body->as.block.statements,
                            function->body->as.block.count, env);

    Value result = nilValue();
    if (g_interpreter.returning) {
        result = g_interpreter.returnValue;
    }

    g_interpreter.returning = savedReturning;
    g_interpreter.returnValue = savedReturnValue;

    return result;
}

/* ============================================================
 * Interpreter lifecycle
 * ============================================================ */

void interpreterInit(void) {
    g_interpreter.globals = newEnvironment(NULL);
    g_interpreter.environment = g_interpreter.globals;
    g_interpreter.returnValue = nilValue();
    g_interpreter.returning = false;

    define("clock", objValue((Obj*)newNative(clockNative, 0, "clock")));
}

Value interpreterRun(Stmt** statements, int count) {
    g_interpreter.environment = g_interpreter.globals;
    g_interpreter.returnValue = nilValue();
    g_interpreter.returning = false;

    for (int i = 0; i < count && !g_interpreter.returning; i++) {
        execute(statements[i]);
    }

    return g_interpreter.returnValue;
}
