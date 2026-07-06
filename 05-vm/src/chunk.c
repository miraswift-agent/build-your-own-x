/*
 * Lantern VM — Chunk Management
 *
 * A chunk is a bytecode compilation unit: code + constants + debug info.
 * Each function in a Lantern program is a chunk.
 */

#define _POSIX_C_SOURCE 200809L
#include "lantern.h"
#include <stdlib.h>
#include <string.h>

/* — Lifecycle — */

void chunk_init(Chunk *c, const char *name, int arity, int local_count) {
    c->code_size = 0;
    c->code_cap = CHUNK_INIT_CODE;
    c->code = malloc(c->code_cap);
    if (!c->code) { c->code_cap = 0; return; }

    c->const_size = 0;
    c->const_cap = CHUNK_INIT_CONSTS;
    c->constants = malloc(sizeof(Value) * c->const_cap);
    if (!c->constants) { c->const_cap = 0; }

    c->lines = malloc(sizeof(int) * c->code_cap);
    if (!c->lines) { /* lines are best-effort */ }

    c->name = name ? strdup(name) : NULL;
    c->arity = arity;
    c->local_count = local_count;
}

void chunk_free(Chunk *c) {
    free(c->code);
    free(c->constants);
    free(c->lines);
    free(c->name);
    c->code = NULL;
    c->constants = NULL;
    c->lines = NULL;
    c->name = NULL;
    c->code_size = 0;
    c->const_size = 0;
}

/* — Writing bytecode — */

static void chunk_grow(Chunk *c, int needed) {
    if (c->code_size + needed <= c->code_cap) return;
    int new_cap = c->code_cap * 2;
    while (new_cap < c->code_size + needed) new_cap *= 2;
    c->code = realloc(c->code, new_cap);
    c->lines = realloc(c->lines, sizeof(int) * new_cap);
    c->code_cap = new_cap;
}

int chunk_write_byte(Chunk *c, uint8_t byte, int line) {
    chunk_grow(c, 1);
    int offset = c->code_size;
    c->code[c->code_size] = byte;
    c->lines[c->code_size] = line;
    c->code_size++;
    return offset;
}

/* Write a 16-bit value in little-endian */
int chunk_write_short(Chunk *c, uint16_t word, int line) {
    chunk_grow(c, 2);
    int offset = c->code_size;
    c->code[c->code_size] = word & 0xFF;
    c->lines[c->code_size] = line;
    c->code_size++;
    c->code[c->code_size] = (word >> 8) & 0xFF;
    c->lines[c->code_size] = line;
    c->code_size++;
    return offset;
}

/* Write a 32-bit value in little-endian */
int chunk_write_int(Chunk *c, uint32_t value, int line) {
    chunk_grow(c, 4);
    int offset = c->code_size;
    for (int i = 0; i < 4; i++) {
        c->code[c->code_size] = (value >> (8 * i)) & 0xFF;
        c->lines[c->code_size] = line;
        c->code_size++;
    }
    return offset;
}

/* Write a 64-bit value in little-endian */
int chunk_write_long(Chunk *c, uint64_t value, int line) {
    chunk_grow(c, 8);
    int offset = c->code_size;
    for (int i = 0; i < 8; i++) {
        c->code[c->code_size] = (value >> (8 * i)) & 0xFF;
        c->lines[c->code_size] = line;
        c->code_size++;
    }
    return offset;
}

/* — Constant pool — */

int chunk_add_constant(Chunk *c, Value value) {
    if (c->const_size >= c->const_cap) {
        int new_cap = c->const_cap * 2;
        c->constants = realloc(c->constants, sizeof(Value) * new_cap);
        c->const_cap = new_cap;
    }
    int idx = c->const_size;
    c->constants[c->const_size] = value;
    c->const_size++;
    return idx;
}

/* — Patching — */

void chunk_patch_short(Chunk *c, int offset, uint16_t value) {
    if (offset + 1 < c->code_size) {
        c->code[offset] = value & 0xFF;
        c->code[offset + 1] = (value >> 8) & 0xFF;
    }
}