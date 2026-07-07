#ifndef LOX_INTERPRETER_H
#define LOX_INTERPRETER_H

#include "environment.h"
#include "ast.h"

typedef struct {
    Environment* globals;
    Environment* environment;
    Value returnValue;
    bool returning;
} Interpreter;

extern Interpreter g_interpreter;

void interpreterInit(void);
Value interpreterRun(Stmt** statements, int count);
void interpreterExecuteBlock(Stmt** statements, int count, Environment* env);
Value callFunction(ObjFunction* function, int argCount, Value* args);

#endif /* LOX_INTERPRETER_H */
