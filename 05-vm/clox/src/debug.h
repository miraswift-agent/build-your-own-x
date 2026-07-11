/*
 * clox — Debug disassembler
 *
 * Pretty-prints a chunk's bytecode and constant pool for testing.
 */

#ifndef CLOX_DEBUG_H
#define CLOX_DEBUG_H

#include "chunk.h"

void disassembleChunk(Chunk *chunk, const char *name);
int disassembleInstruction(Chunk *chunk, int offset);

#endif /* CLOX_DEBUG_H */
