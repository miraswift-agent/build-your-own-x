/*
 * clox — Native function implementation
 */

#include <time.h>

#include "native.h"
#include "object.h"
#include "vm.h"

static Value clockNative(int argCount, Value *args) {
    (void)argCount;
    (void)args;
    return NUMBER_VAL((double)clock() / CLOCKS_PER_SEC);
}

void defineNatives(void) {
    ObjString *name = copyString("clock", 5);
    push(OBJ_VAL(name));
    tableSet(&vm.globals, name, OBJ_VAL(newNative(clockNative)));
    pop();
}
