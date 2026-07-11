#ifndef LOX_ENVIRONMENT_H
#define LOX_ENVIRONMENT_H

#include "value.h"

#define ENV_BUCKETS 64

typedef struct Entry {
    char* key;
    Value value;
    struct Entry* next;
} Entry;

struct Environment {
    Entry* buckets[ENV_BUCKETS];
    Environment* enclosing;
};

Environment* newEnvironment(Environment* enclosing);
void envDefine(Environment* env, const char* name, Value value);
bool envGet(Environment* env, const char* name, Value* out);
bool envGetAt(Environment* env, int distance, const char* name, Value* out);
bool envAssign(Environment* env, const char* name, Value value);
bool envAssignAt(Environment* env, int distance, const char* name, Value value);

#endif /* LOX_ENVIRONMENT_H */
