#include "environment.h"

#include <string.h>

Environment* newEnvironment(Environment* enclosing) {
    Environment* env = ALLOCATE(Environment);
    env->enclosing = enclosing;
    memset(env->buckets, 0, sizeof(env->buckets));
    return env;
}

static unsigned hashString(const char* key) {
    unsigned hash = 2166136261u;
    for (int i = 0; key[i] != '\0'; i++) {
        hash ^= (unsigned char)key[i];
        hash *= 16777619;
    }
    return hash;
}

static Entry* findEntryInBucket(Entry* bucket, const char* name) {
    for (Entry* entry = bucket; entry != NULL; entry = entry->next) {
        if (strcmp(entry->key, name) == 0) {
            return entry;
        }
    }
    return NULL;
}

void envDefine(Environment* env, const char* name, Value value) {
    unsigned index = hashString(name) % ENV_BUCKETS;
    Entry* existing = findEntryInBucket(env->buckets[index], name);
    if (existing != NULL) {
        existing->value = value;
        return;
    }

    Entry* entry = ALLOCATE(Entry);
    entry->key = loxCopyString(name, strlen(name));
    entry->value = value;
    entry->next = env->buckets[index];
    env->buckets[index] = entry;
}

bool envGet(Environment* env, const char* name, Value* out) {
    unsigned index = hashString(name) % ENV_BUCKETS;
    Entry* entry = findEntryInBucket(env->buckets[index], name);
    if (entry != NULL) {
        *out = entry->value;
        return true;
    }

    if (env->enclosing != NULL) {
        return envGet(env->enclosing, name, out);
    }

    return false;
}

bool envGetAt(Environment* env, int distance, const char* name, Value* out) {
    Environment* current = env;
    for (int i = 0; i < distance; i++) {
        current = current->enclosing;
    }

    unsigned index = hashString(name) % ENV_BUCKETS;
    Entry* entry = findEntryInBucket(current->buckets[index], name);
    if (entry != NULL) {
        *out = entry->value;
        return true;
    }
    return false;
}

bool envAssign(Environment* env, const char* name, Value value) {
    unsigned index = hashString(name) % ENV_BUCKETS;
    Entry* entry = findEntryInBucket(env->buckets[index], name);
    if (entry != NULL) {
        entry->value = value;
        return true;
    }

    if (env->enclosing != NULL) {
        return envAssign(env->enclosing, name, value);
    }

    return false;
}

bool envAssignAt(Environment* env, int distance, const char* name, Value value) {
    Environment* current = env;
    for (int i = 0; i < distance; i++) {
        current = current->enclosing;
    }

    unsigned index = hashString(name) % ENV_BUCKETS;
    Entry* entry = findEntryInBucket(current->buckets[index], name);
    if (entry != NULL) {
        entry->value = value;
        return true;
    }
    return false;
}
