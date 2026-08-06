/*
 * clox — Bytecode chunk
 *
 * A chunk is the compiled output for a single function: a sequence of
 * bytecode instructions, their source lines, and a constant pool.
 */

#ifndef CLOX_CHUNK_H
#define CLOX_CHUNK_H

#include "common.h"
#include "value.h"

typedef enum {
    OP_CONSTANT,
    OP_NIL,
    OP_TRUE,
    OP_FALSE,
    OP_POP,
    OP_GET_LOCAL,
    OP_SET_LOCAL,
    OP_GET_GLOBAL,
    OP_DEFINE_GLOBAL,
    OP_SET_GLOBAL,
    OP_GET_UPVALUE,
    OP_SET_UPVALUE,
    OP_GET_PROPERTY,
    OP_SET_PROPERTY,
    OP_GET_SUPER,
    OP_EQUAL,
    OP_GREATER,
    OP_LESS,
    OP_ADD,
    OP_SUBTRACT,
    OP_MULTIPLY,
    OP_DIVIDE,
    OP_NOT,
    OP_NEGATE,
    OP_PRINT,
    OP_JUMP,
    OP_JUMP_IF_FALSE,
    OP_LOOP,
    OP_CALL,
    OP_CLOSURE,
    OP_CLOSE_UPVALUE,
    OP_RETURN,
    OP_CLASS,
    OP_INHERIT,
    OP_METHOD,
    OP_ARRAY,  /* Stage 12b-i: build ObjArray from top N stack values; operand = element count */
    OP_INDEX_GET,  /* Stage 12b-ii: read a[i]; stack: ..., array, index -> ..., element */
    OP_INDEX_SET,  /* Stage 12b-iii: write a[i] = v; stack: ..., array, index, value -> (no result) */
    OP_IMPORT, /* Stage 64.2: load module from path constant; push ObjModule */
    OP_DUP     /* Stage 64.6: duplicate top of stack (selective import bind loop) */
} OpCode;

typedef struct {
    int count;
    int capacity;
    uint8_t *code;
    int *lines;
    ValueArray constants;
} Chunk;

void initChunk(Chunk *chunk);
void freeChunk(Chunk *chunk);
int addConstant(Chunk *chunk, Value value);
int writeChunk(Chunk *chunk, uint8_t byte, int line);

#endif /* CLOX_CHUNK_H */
