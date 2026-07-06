/*
 * Lantern VM — Disassembler
 *
 * Turns Lantern bytecode back into human-readable instructions.
 * "You can't fix what you can't see."
 */

#include "lantern.h"
#include <stdio.h>
#include <string.h>
#include <inttypes.h>

static const char* opcode_name(uint8_t op) {
    switch (op) {
    case OP_HALT:          return "HALT";
    case OP_CONST:         return "CONST";
    case OP_INT8:          return "INT8";
    case OP_INT32:         return "INT32";
    case OP_FLOAT64:       return "FLOAT64";
    case OP_TRUE:          return "TRUE";
    case OP_FALSE:         return "FALSE";
    case OP_NULL:          return "NULL";
    case OP_POP:           return "POP";
    case OP_DUP:           return "DUP";
    case OP_LOAD_LOCAL:    return "LOAD_LOCAL";
    case OP_STORE_LOCAL:   return "STORE_LOCAL";
    case OP_ADD:           return "ADD";
    case OP_SUBTRACT:      return "SUB";
    case OP_MULTIPLY:      return "MUL";
    case OP_DIVIDE:        return "DIV";
    case OP_MODULO:        return "MOD";
    case OP_NEGATE:        return "NEG";
    case OP_EQUAL:         return "EQ";
    case OP_NOT_EQUAL:     return "NE";
    case OP_LESS:          return "LT";
    case OP_LESS_EQUAL:    return "LE";
    case OP_GREATER:       return "GT";
    case OP_GREATER_EQUAL: return "GE";
    case OP_NOT:           return "NOT";
    case OP_JUMP:          return "JUMP";
    case OP_JUMP_IF_FALSE: return "JUMP_IF_FALSE";
    case OP_JUMP_IF_TRUE:  return "JUMP_IF_TRUE";
    case OP_LOOP:          return "LOOP";
    case OP_CALL:          return "CALL";
    case OP_RETURN:        return "RETURN";
    case OP_PRINT:         return "PRINT";
    case OP_PRINT_LN:      return "PRINT_LN";
    default:               return "???";
    }
}

static int disassemble_simple(Chunk *chunk, int offset) {
    printf("%-16s\n", opcode_name(chunk->code[offset]));
    return offset + 1;
}

static int disassemble_byte(Chunk *chunk, int offset) {
    uint8_t slot = chunk->code[offset + 1];
    printf("%-16s %d\n", opcode_name(chunk->code[offset]), slot);
    return offset + 2;
}

static int disassemble_short(Chunk *chunk, int offset) {
    uint16_t val = chunk->code[offset + 1] | ((uint16_t)chunk->code[offset + 2] << 8);
    int16_t sval = (int16_t)val;
    printf("%-16s %d (-> %d)\n", opcode_name(chunk->code[offset]), sval, offset + 3 + sval);
    return offset + 3;
}

static int disassemble_const(Chunk *chunk, int offset) {
    uint16_t idx = chunk->code[offset + 1] | ((uint16_t)chunk->code[offset + 2] << 8);
    printf("%-16s %u (", opcode_name(chunk->code[offset]), idx);
    if (idx < (uint16_t)chunk->const_size) {
        value_print(chunk->constants[idx]);
    } else {
        printf("OOB");
    }
    printf(")\n");
    return offset + 3;
}

static int disassemble_int8(Chunk *chunk, int offset) {
    int8_t val = (int8_t)chunk->code[offset + 1];
    printf("%-16s %d\n", opcode_name(chunk->code[offset]), val);
    return offset + 2;
}

static int disassemble_int32(Chunk *chunk, int offset) {
    int32_t val = 0;
    for (int i = 0; i < 4; i++) {
        val |= ((uint32_t)chunk->code[offset + 1 + i]) << (8 * i);
    }
    printf("%-16s %" PRId32 "\n", opcode_name(chunk->code[offset]), val);
    return offset + 5;
}

static int disassemble_float64(Chunk *chunk, int offset) {
    double val;
    memcpy(&val, &chunk->code[offset + 1], 8);
    printf("%-16s %g\n", opcode_name(chunk->code[offset]), val);
    return offset + 9;
}

static int disassemble_call(Chunk *chunk, int offset) {
    uint16_t func_idx = chunk->code[offset + 1] | ((uint16_t)chunk->code[offset + 2] << 8);
    uint8_t argc = chunk->code[offset + 3];
    printf("%-16s func=%d argc=%d\n", opcode_name(chunk->code[offset]), func_idx, argc);
    return offset + 4;
}

int lvm_disassemble_instruction(Chunk *chunk, int offset) {
    printf("%04X ", offset);
    if (offset > 0 && chunk->lines && chunk->lines[offset] == chunk->lines[offset - 1]) {
        printf("   | ");
    } else {
        printf("%4d ", chunk->lines ? chunk->lines[offset] : 0);
    }

    uint8_t op = chunk->code[offset];
    switch (op) {
    /* 1-byte instructions */
    case OP_HALT:
    case OP_TRUE:
    case OP_FALSE:
    case OP_NULL:
    case OP_POP:
    case OP_DUP:
    case OP_ADD:
    case OP_SUBTRACT:
    case OP_MULTIPLY:
    case OP_DIVIDE:
    case OP_MODULO:
    case OP_NEGATE:
    case OP_EQUAL:
    case OP_NOT_EQUAL:
    case OP_LESS:
    case OP_LESS_EQUAL:
    case OP_GREATER:
    case OP_GREATER_EQUAL:
    case OP_NOT:
    case OP_RETURN:
    case OP_PRINT:
    case OP_PRINT_LN:
        return disassemble_simple(chunk, offset);

    /* 2-byte instructions */
    case OP_INT8:
        return disassemble_int8(chunk, offset);
    case OP_LOAD_LOCAL:
    case OP_STORE_LOCAL:
        return disassemble_byte(chunk, offset);

    /* 3-byte instructions */
    case OP_CONST:
        return disassemble_const(chunk, offset);
    case OP_JUMP:
    case OP_JUMP_IF_FALSE:
    case OP_JUMP_IF_TRUE:
    case OP_LOOP:
        return disassemble_short(chunk, offset);

    /* 4-byte instructions */
    case OP_CALL:
        return disassemble_call(chunk, offset);

    /* 5-byte instructions */
    case OP_INT32:
        return disassemble_int32(chunk, offset);

    /* 9-byte instructions */
    case OP_FLOAT64:
        return disassemble_float64(chunk, offset);

    default:
        printf("UNKNOWN opcode 0x%02X\n", op);
        return offset + 1;
    }
}

void lvm_disassemble_chunk(Chunk *chunk, const char *name) {
    printf("=== %s ===\n", name);
    printf("  Arity: %d, Locals: %d, Constants: %d, Code: %d bytes\n",
           chunk->arity, chunk->local_count, chunk->const_size, chunk->code_size);

    /* Print constant pool */
    if (chunk->const_size > 0) {
        printf("  Constants:\n");
        for (int i = 0; i < chunk->const_size; i++) {
            printf("    %04d: ", i);
            value_println(chunk->constants[i]);
        }
    }

    /* Disassemble each instruction */
    int offset = 0;
    while (offset < chunk->code_size) {
        offset = lvm_disassemble_instruction(chunk, offset);
    }
}