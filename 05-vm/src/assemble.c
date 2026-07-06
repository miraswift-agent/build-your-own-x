/*
 * Lantern VM — Assembler
 *
 * Translates human-readable assembly into Lantern bytecode.
 *
 * Syntax:
 *   ; comment
 *   .func <name> <arity> <local_count>
 *   @label:
 *   <opcode> [operand]
 *
 * Opcodes: halt, const, int8, int32, float64, true, false, null,
 *          pop, dup, load_local, store_local,
 *          add, sub, mul, div, mod, neg,
 *          eq, ne, lt, le, gt, ge, not,
 *          jump, jump_if_false, jump_if_true, loop,
 *          call, return, print, println
 */

#define _GNU_SOURCE
#include "lantern.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

/* — Error strings — */

const char* asm_error_string(AsmError err) {
    switch (err) {
    case ASM_OK:                    return "OK";
    case ASM_ERR_SYNTAX:            return "Syntax error";
    case ASM_ERR_UNKNOWN_OPCODE:    return "Unknown opcode";
    case ASM_ERR_UNDEFINED_LABEL:   return "Undefined label";
    case ASM_ERR_DUPLICATE_LABEL:   return "Duplicate label";
    case ASM_ERR_INVALID_OPERAND:   return "Invalid operand";
    case ASM_ERR_TOO_MANY_FUNCTIONS:return "Too many functions";
    case ASM_ERR_INTERNAL:          return "Internal error";
    default:                        return "Unknown error";
    }
}

/* — Label/Fixup helpers — */

static void free_labels(AsmLabel **labels) {
    AsmLabel *l = *labels;
    while (l) {
        AsmLabel *next = l->next;
        free(l);
        l = next;
    }
    *labels = NULL;
}

static void free_fixups(AsmFixup **fixups) {
    AsmFixup *f = *fixups;
    while (f) {
        AsmFixup *next = f->next;
        free(f);
        f = next;
    }
    *fixups = NULL;
}

static AsmLabel* find_label(AsmLabel *labels, const char *name) {
    for (AsmLabel *l = labels; l; l = l->next) {
        if (strcmp(l->name, name) == 0) return l;
    }
    return NULL;
}

/* — Opcode name lookup — */

typedef struct {
    const char *name;
    OpCode       opcode;
    int          operand_bytes;  /* 0=no operand, 1=byte, 2=short, 4=int32, 8=float64 */
} OpcodeEntry;

static const OpcodeEntry opcode_table[] = {
    {"halt",           OP_HALT,          0},
    {"const",          OP_CONST,         2},
    {"int8",           OP_INT8,          1},
    {"int32",          OP_INT32,         4},
    {"float64",        OP_FLOAT64,       8},
    {"true",           OP_TRUE,          0},
    {"false",          OP_FALSE,         0},
    {"null",           OP_NULL,          0},
    {"pop",            OP_POP,           0},
    {"dup",            OP_DUP,           0},
    {"load_local",     OP_LOAD_LOCAL,    1},
    {"store_local",    OP_STORE_LOCAL,   1},
    {"add",            OP_ADD,           0},
    {"sub",            OP_SUBTRACT,      0},
    {"mul",            OP_MULTIPLY,      0},
    {"div",            OP_DIVIDE,        0},
    {"mod",            OP_MODULO,        0},
    {"neg",            OP_NEGATE,        0},
    {"eq",             OP_EQUAL,         0},
    {"ne",             OP_NOT_EQUAL,     0},
    {"lt",             OP_LESS,          0},
    {"le",             OP_LESS_EQUAL,    0},
    {"gt",             OP_GREATER,       0},
    {"ge",             OP_GREATER_EQUAL, 0},
    {"not",            OP_NOT,           0},
    {"jump",           OP_JUMP,          2},  /* label */
    {"jump_if_false",  OP_JUMP_IF_FALSE, 2},  /* label */
    {"jump_if_true",   OP_JUMP_IF_TRUE,  2},  /* label */
    {"loop",           OP_LOOP,          2},  /* label */
    {"call",           OP_CALL,          3},  /* func_idx:2 + argc:1 */
    {"return",         OP_RETURN,        0},
    {"print",          OP_PRINT,         0},
    {"println",        OP_PRINT_LN,      0},
    {NULL, 0, 0}
};

static const OpcodeEntry* find_opcode(const char *name) {
    for (const OpcodeEntry *e = opcode_table; e->name; e++) {
        if (strcmp(e->name, name) == 0) return e;
    }
    return NULL;
}

/* — Line parsing helpers — */

static char* trim(char *s) {
    while (*s && isspace((unsigned char)*s)) s++;
    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) *end-- = '\0';
    return s;
}

static int parse_int(const char *s, int64_t *out) {
    char *end;
    long long val = strtoll(s, &end, 0);  /* auto-detect base (0x, 0o, decimal) */
    if (end == s || *end != '\0') return -1;
    *out = (int64_t)val;
    return 0;
}

static int parse_double(const char *s, double *out) {
    char *end;
    double val = strtod(s, &end);
    if (end == s || *end != '\0') return -1;
    *out = val;
    return 0;
}

/* — Assembler init/free — */

void asm_init(Assembler *a) {
    memset(a, 0, sizeof(Assembler));
}

void asm_free(Assembler *a) {
    for (int i = 0; i < a->chunk_count; i++) {
        chunk_free(&a->chunks[i]);
    }
    free_labels(&a->labels);
    free_fixups(&a->fixups);
}

int asm_func_count(Assembler *a) {
    return a->chunk_count;
}

Chunk* asm_get_chunk(Assembler *a, int index) {
    if (index < 0 || index >= a->chunk_count) return NULL;
    return &a->chunks[index];
}

Chunk* asm_get_main(Assembler *a) {
    if (a->chunk_count == 0) return NULL;
    return &a->chunks[0];
}

/* — Fixup resolution — */

static AsmError resolve_fixups(Assembler *a, Chunk *chunk) {
    AsmFixup *f = a->fixups;
    while (f) {
        AsmLabel *label = find_label(a->labels, f->label);
        if (!label) {
            snprintf(a->error_msg, sizeof(a->error_msg),
                     "Undefined label '%s'", f->label);
            a->error = ASM_ERR_UNDEFINED_LABEL;
            return a->error;
        }
        /* Calculate relative offset from byte AFTER the jump instruction */
        int16_t offset = (int16_t)(label->offset - (f->instr_offset + 3));
        chunk_patch_short(chunk, f->offset, (uint16_t)offset);
        f = f->next;
    }
    return ASM_OK;
}

/* — Main parse function — */

AsmError asm_parse(Assembler *a, const char *source) {
    /* Make a mutable copy */
    char *buf = strdup(source);
    if (!buf) {
        a->error = ASM_ERR_INTERNAL;
        return a->error;
    }

    Chunk *current_chunk = NULL;
    int line_num = 0;

    char *saveptr;
    char *line = strtok_r(buf, "\n", &saveptr);

    while (line) {
        line_num++;
        a->current_line = line_num;

        /* Trim */
        char *trimmed = trim(line);

        /* Skip empty lines and comments */
        if (*trimmed == '\0' || *trimmed == ';') {
            line = strtok_r(NULL, "\n", &saveptr);
            continue;
        }

        /* Function definition: .func <name> <arity> <local_count> */
        if (strncmp(trimmed, ".func", 5) == 0) {
            /* Resolve fixups for previous function */
            if (current_chunk) {
                AsmError err = resolve_fixups(a, current_chunk);
                if (err != ASM_OK) { free(buf); return err; }
                free_labels(&a->labels);
                free_fixups(&a->fixups);
            }

            /* Parse function header */
            char fname[64];
            int arity, local_count;
            if (sscanf(trimmed + 5, " %63s %d %d", fname, &arity, &local_count) != 3) {
                snprintf(a->error_msg, sizeof(a->error_msg),
                         "Line %d: Invalid .func directive: '%s'", line_num, trimmed);
                a->error = ASM_ERR_SYNTAX;
                free(buf);
                return a->error;
            }

            if (a->chunk_count >= LANTERN_FUNCS_MAX) {
                a->error = ASM_ERR_TOO_MANY_FUNCTIONS;
                free(buf);
                return a->error;
            }

            chunk_init(&a->chunks[a->chunk_count], fname, arity, local_count);
            current_chunk = &a->chunks[a->chunk_count];
            a->chunk_count++;

            line = strtok_r(NULL, "\n", &saveptr);
            continue;
        }

        /* If no current chunk, create a default main */
        if (!current_chunk) {
            if (a->chunk_count >= LANTERN_FUNCS_MAX) {
                a->error = ASM_ERR_TOO_MANY_FUNCTIONS;
                free(buf);
                return a->error;
            }
            chunk_init(&a->chunks[a->chunk_count], "main", 0, 0);
            current_chunk = &a->chunks[a->chunk_count];
            a->chunk_count++;
        }

        /* Label definition: @name: or @name */
        if (*trimmed == '@') {
            char *colon = strchr(trimmed, ':');
            if (colon) *colon = '\0';
            char *label_name = trimmed + 1;

            /* Check for duplicate */
            if (find_label(a->labels, label_name)) {
                snprintf(a->error_msg, sizeof(a->error_msg),
                         "Line %d: Duplicate label '%s'", line_num, label_name);
                a->error = ASM_ERR_DUPLICATE_LABEL;
                free(buf);
                return a->error;
            }

            AsmLabel *label = malloc(sizeof(AsmLabel));
            strncpy(label->name, label_name, sizeof(label->name) - 1);
            label->name[sizeof(label->name) - 1] = '\0';
            label->offset = current_chunk->code_size;
            label->next = a->labels;
            a->labels = label;

            /* Resolve any fixups that reference this label */
            /* (We'll do a full resolve at end of function or end of parse) */

            line = strtok_r(NULL, "\n", &saveptr);
            continue;
        }

        /* Instruction */
        char opcode_name[32];
        char operand1[64] = {0};
        char operand2[64] = {0};
        int ntok = sscanf(trimmed, "%31s %63s %63s", opcode_name, operand1, operand2);

        const OpcodeEntry *entry = find_opcode(opcode_name);
        if (!entry) {
            snprintf(a->error_msg, sizeof(a->error_msg),
                     "Line %d: Unknown opcode '%s'", line_num, opcode_name);
            a->error = ASM_ERR_UNKNOWN_OPCODE;
            free(buf);
            return a->error;
        }

        int instr_offset = current_chunk->code_size;
        chunk_write_byte(current_chunk, entry->opcode, line_num);

        switch (entry->opcode) {
        /* No-operand instructions */
        case OP_HALT: case OP_TRUE: case OP_FALSE: case OP_NULL:
        case OP_POP: case OP_DUP:
        case OP_ADD: case OP_SUBTRACT: case OP_MULTIPLY: case OP_DIVIDE: case OP_MODULO:
        case OP_NEGATE:
        case OP_EQUAL: case OP_NOT_EQUAL: case OP_LESS: case OP_LESS_EQUAL:
        case OP_GREATER: case OP_GREATER_EQUAL:
        case OP_NOT:
        case OP_RETURN:
        case OP_PRINT: case OP_PRINT_LN:
            if (ntok > 1) {
                snprintf(a->error_msg, sizeof(a->error_msg),
                         "Line %d: '%s' takes no operands", line_num, opcode_name);
                a->error = ASM_ERR_INVALID_OPERAND;
                free(buf);
                return a->error;
            }
            break;

        /* 1-byte operand */
        case OP_INT8: {
            int64_t val;
            if (parse_int(operand1, &val) != 0 || val < -128 || val > 127) {
                snprintf(a->error_msg, sizeof(a->error_msg),
                         "Line %d: int8 operand must be -128..127, got '%s'", line_num, operand1);
                a->error = ASM_ERR_INVALID_OPERAND;
                free(buf);
                return a->error;
            }
            chunk_write_byte(current_chunk, (uint8_t)(int8_t)val, line_num);
            break;
        }
        case OP_LOAD_LOCAL:
        case OP_STORE_LOCAL: {
            int64_t val;
            if (parse_int(operand1, &val) != 0 || val < 0 || val > 255) {
                snprintf(a->error_msg, sizeof(a->error_msg),
                         "Line %d: local slot must be 0..255, got '%s'", line_num, operand1);
                a->error = ASM_ERR_INVALID_OPERAND;
                free(buf);
                return a->error;
            }
            chunk_write_byte(current_chunk, (uint8_t)val, line_num);
            break;
        }

        /* 2-byte operand (constant pool index) */
        case OP_CONST: {
            int64_t idx;
            if (parse_int(operand1, &idx) != 0 || idx < 0 || idx > 65535) {
                snprintf(a->error_msg, sizeof(a->error_msg),
                         "Line %d: const index must be 0..65535, got '%s'", line_num, operand1);
                a->error = ASM_ERR_INVALID_OPERAND;
                free(buf);
                return a->error;
            }
            chunk_write_short(current_chunk, (uint16_t)idx, line_num);
            break;
        }

        /* Jump instructions — operand is a label name */
        case OP_JUMP:
        case OP_JUMP_IF_FALSE:
        case OP_JUMP_IF_TRUE:
        case OP_LOOP: {
            /* Strip leading '@' from label reference to match label defs */
            const char *ref_name = operand1;
            if (*ref_name == '@') ref_name++;

            /* Write placeholder offset, record fixup */
            int fixup_offset = current_chunk->code_size;
            chunk_write_short(current_chunk, 0, line_num);  /* placeholder */

            /* Try to resolve label now */
            AsmLabel *label = find_label(a->labels, ref_name);
            if (label) {
                /* Label is defined before this jump — resolve immediately */
                int16_t offset = (int16_t)(label->offset - (instr_offset + 3));
                chunk_patch_short(current_chunk, fixup_offset, (uint16_t)offset);
            } else {
                /* Label not yet defined — record fixup */
                AsmFixup *fixup = malloc(sizeof(AsmFixup));
                strncpy(fixup->label, ref_name, sizeof(fixup->label) - 1);
                fixup->label[sizeof(fixup->label) - 1] = '\0';
                fixup->offset = fixup_offset;
                fixup->instr_offset = instr_offset;
                fixup->next = a->fixups;
                a->fixups = fixup;
            }
            break;
        }

        /* Call instruction: call <func_idx> <argc> */
        case OP_CALL: {
            int64_t func_idx, argc;
            if (ntok < 3 ||
                parse_int(operand1, &func_idx) != 0 ||
                parse_int(operand2, &argc) != 0) {
                snprintf(a->error_msg, sizeof(a->error_msg),
                         "Line %d: call requires <func_idx> <argc>", line_num);
                a->error = ASM_ERR_INVALID_OPERAND;
                free(buf);
                return a->error;
            }
            chunk_write_short(current_chunk, (uint16_t)func_idx, line_num);
            chunk_write_byte(current_chunk, (uint8_t)argc, line_num);
            break;
        }

        /* 4-byte operand (int32) */
        case OP_INT32: {
            int64_t val;
            if (parse_int(operand1, &val) != 0) {
                snprintf(a->error_msg, sizeof(a->error_msg),
                         "Line %d: int32 operand must be an integer, got '%s'", line_num, operand1);
                a->error = ASM_ERR_INVALID_OPERAND;
                free(buf);
                return a->error;
            }
            chunk_write_int(current_chunk, (uint32_t)(int32_t)val, line_num);
            break;
        }

        /* 8-byte operand (float64) */
        case OP_FLOAT64: {
            double val;
            if (parse_double(operand1, &val) != 0) {
                snprintf(a->error_msg, sizeof(a->error_msg),
                         "Line %d: float64 operand must be a number, got '%s'", line_num, operand1);
                a->error = ASM_ERR_INVALID_OPERAND;
                free(buf);
                return a->error;
            }
            /* Write as raw bytes */
            uint64_t bits;
            memcpy(&bits, &val, 8);
            chunk_write_long(current_chunk, bits, line_num);
            break;
        }

        default:
            snprintf(a->error_msg, sizeof(a->error_msg),
                     "Line %d: Unhandled opcode '%s' (0x%02X)", line_num, opcode_name, entry->opcode);
            a->error = ASM_ERR_INTERNAL;
            free(buf);
            return a->error;
        }

        line = strtok_r(NULL, "\n", &saveptr);
    }

    /* Resolve fixups for the last function */
    if (current_chunk) {
        AsmError err = resolve_fixups(a, current_chunk);
        if (err != ASM_OK) { free(buf); return err; }
    }

    free(buf);
    return ASM_OK;
}

AsmError asm_parse_file(Assembler *a, const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) {
        snprintf(a->error_msg, sizeof(a->error_msg), "Cannot open file '%s'", path);
        a->error = ASM_ERR_INTERNAL;
        return a->error;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *buf = malloc(size + 1);
    fread(buf, 1, size, f);
    buf[size] = '\0';
    fclose(f);

    AsmError err = asm_parse(a, buf);
    free(buf);
    return err;
}