/*
 * Lantern VM — Stage 2: Custom ISA
 *
 * A stack-based virtual machine with:
 * - Variable-length instructions (1-9 bytes)
 * - Typed values (int64, float64, bool, null)
 * - Function calls with stack frames
 * - Local variables (frame-relative addressing)
 * - An assembler that translates human-readable text to bytecode
 *
 * "Instruction set design is an act of taste. Every opcode you add
 *  is a promise to support forever. Every opcode you omit is a tax
 *  on the programs that run on your machine."
 */

#ifndef LANTERN_H
#define LANTERN_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* ============================================================
 * Value System — Tagged Union
 * ============================================================ */

typedef enum {
    VAL_INT,
    VAL_FLOAT,
    VAL_BOOL,
    VAL_NULL,
} ValType;

typedef struct {
    ValType type;
    union {
        int64_t integer;
        double  floating;
        bool    boolean;
    } as;
} Value;

/* Value constructors */
#define INT_VAL(v)      ((Value){.type = VAL_INT,   .as.integer = (v)})
#define FLOAT_VAL(v)    ((Value){.type = VAL_FLOAT, .as.floating = (v)})
#define BOOL_VAL(v)     ((Value){.type = VAL_BOOL,  .as.boolean  = (v)})
#define NULL_VAL        ((Value){.type = VAL_NULL,  .as.integer  = 0})

/* Value operations */
bool      value_is_truthy(Value v);
bool      value_equal(Value a, Value b);
Value     value_add(Value a, Value b);
Value     value_subtract(Value a, Value b);
Value     value_multiply(Value a, Value b);
Value     value_divide(Value a, Value b);
Value     value_modulo(Value a, Value b);
Value     value_negate(Value v);
Value     value_not(Value v);
void      value_print(Value v);
void      value_println(Value v);
const char* value_type_name(ValType type);

/* ============================================================
 * Instruction Opcodes
 * ============================================================ */

typedef enum {
    /* Control */
    OP_HALT          = 0x00,  /* 1 byte: halt execution */

    /* Constants */
    OP_CONST          = 0x01, /* 3 bytes: push constants[idx] (uint16 index) */
    OP_INT8           = 0x02, /* 2 bytes: push inline int8 */
    OP_INT32          = 0x03, /* 5 bytes: push inline int32 (LE) */
    OP_FLOAT64        = 0x04, /* 9 bytes: push inline float64 (LE) */
    OP_TRUE           = 0x05, /* 1 byte: push true */
    OP_FALSE          = 0x06, /* 1 byte: push false */
    OP_NULL           = 0x07, /* 1 byte: push null */

    /* Stack */
    OP_POP            = 0x08, /* 1 byte: pop and discard */
    OP_DUP            = 0x09, /* 1 byte: duplicate top */

    /* Local variables (frame-relative) */
    OP_LOAD_LOCAL     = 0x0A, /* 2 bytes: push frame_base[slot] */
    OP_STORE_LOCAL    = 0x0B, /* 2 bytes: pop → frame_base[slot] */

    /* Arithmetic — pop a, pop b, push result (b op a) */
    OP_ADD            = 0x10, /* 1 byte */
    OP_SUBTRACT       = 0x11, /* 1 byte */
    OP_MULTIPLY       = 0x12, /* 1 byte */
    OP_DIVIDE         = 0x13, /* 1 byte */
    OP_MODULO         = 0x14, /* 1 byte */
    OP_NEGATE         = 0x15, /* 1 byte: pop a, push -a */

    /* Comparison — pop a, pop b, push bool (b op a) */
    OP_EQUAL          = 0x20, /* 1 byte */
    OP_NOT_EQUAL      = 0x21, /* 1 byte */
    OP_LESS           = 0x22, /* 1 byte */
    OP_LESS_EQUAL     = 0x23, /* 1 byte */
    OP_GREATER        = 0x24, /* 1 byte */
    OP_GREATER_EQUAL  = 0x25, /* 1 byte */

    /* Logic */
    OP_NOT            = 0x30, /* 1 byte: pop a, push !a */

    /* Control flow — offset is signed 16-bit, relative to byte after this instruction */
    OP_JUMP           = 0x40, /* 3 bytes: unconditional jump */
    OP_JUMP_IF_FALSE  = 0x41, /* 3 bytes: pop; if falsy, jump */
    OP_JUMP_IF_TRUE   = 0x42, /* 3 bytes: pop; if truthy, jump */
    OP_LOOP           = 0x43, /* 3 bytes: jump backward (semantic hint for loops) */

    /* Functions */
    OP_CALL           = 0x50, /* 4 bytes: [opcode][func_idx:2][argc:1] */
    OP_RETURN         = 0x51, /* 1 byte: pop return value, restore frame */

    /* Built-in */
    OP_PRINT          = 0x60, /* 1 byte: pop and print */
    OP_PRINT_LN       = 0x61, /* 1 byte: pop, print, newline */

    /* Invalid */
    OP_INVALID        = 0xFF,
} OpCode;

/* ============================================================
 * Chunk — Compilation Unit
 * ============================================================ */

#define CHUNK_INIT_CODE    256
#define CHUNK_INIT_CONSTS   16

typedef struct {
    uint8_t *code;          /* Bytecode */
    int      code_size;      /* Bytes written */
    int      code_cap;       /* Allocated capacity */
    Value   *constants;      /* Constant pool */
    int      const_size;     /* Constants written */
    int      const_cap;      /* Allocated capacity */
    int     *lines;          /* Source line per bytecode byte */
    char    *name;           /* Function name (strdup'd) */
    int      arity;          /* Number of parameters */
    int      local_count;    /* Total locals (including params) */
} Chunk;

void chunk_init(Chunk *c, const char *name, int arity, int local_count);
void chunk_free(Chunk *c);
int  chunk_write_byte(Chunk *c, uint8_t byte, int line);
int  chunk_write_short(Chunk *c, uint16_t word, int line);
int  chunk_write_int(Chunk *c, uint32_t value, int line);
int  chunk_write_long(Chunk *c, uint64_t value, int line);
int  chunk_add_constant(Chunk *c, Value value);
void chunk_patch_short(Chunk *c, int offset, uint16_t value);

/* ============================================================
 * VM Error Codes
 * ============================================================ */

typedef enum {
    LVM_OK = 0,
    LVM_ERR_STACK_OVERFLOW,
    LVM_ERR_STACK_UNDERFLOW,
    LVM_ERR_TYPE_ERROR,
    LVM_ERR_DIVISION_BY_ZERO,
    LVM_ERR_UNDEFINED_LOCAL,
    LVM_ERR_INVALID_OPCODE,
    LVM_ERR_CALL_DEPTH_EXCEEDED,
    LVM_ERR_ARITY_MISMATCH,
    LVM_ERR_RUNTIME,
} LVMError;

const char* lvm_error_string(LVMError err);

/* ============================================================
 * VM State
 * ============================================================ */

#define LANTERN_STACK_MAX    65536
#define LANTERN_FRAMES_MAX   1024
#define LANTERN_FUNCS_MAX     256

typedef struct {
    uint8_t *ip;          /* Instruction pointer into chunk->code */
    Value   *stack_base;  /* Base of this frame's locals */
    Chunk   *chunk;       /* Chunk being executed */
} CallFrame;

typedef struct {
    Value      stack[LANTERN_STACK_MAX];
    Value     *stack_top;
    CallFrame  frames[LANTERN_FRAMES_MAX];
    int        frame_count;
    Chunk     *functions[LANTERN_FUNCS_MAX];
    int        func_count;
    LVMError   error;
    bool       running;
    char       error_msg[256];
} LanternVM;

/* VM lifecycle */
LanternVM *lvm_new(void);
void       lvm_free(LanternVM *vm);
void       lvm_reset(LanternVM *vm);

/* Add a function chunk to the VM, returns its index */
int  lvm_add_function(LanternVM *vm, Chunk *chunk);

/* Execute starting from function 0 (main) */
LVMError lvm_run(LanternVM *vm);

/* Execute one instruction */
LVMError lvm_step(LanternVM *vm);

/* Disassembler */
void lvm_disassemble_chunk(Chunk *chunk, const char *name);
int  lvm_disassemble_instruction(Chunk *chunk, int offset);

/* ============================================================
 * Assembler
 * ============================================================ */

typedef enum {
    ASM_OK = 0,
    ASM_ERR_SYNTAX,
    ASM_ERR_UNKNOWN_OPCODE,
    ASM_ERR_UNDEFINED_LABEL,
    ASM_ERR_DUPLICATE_LABEL,
    ASM_ERR_INVALID_OPERAND,
    ASM_ERR_TOO_MANY_FUNCTIONS,
    ASM_ERR_INTERNAL,
} AsmError;

const char* asm_error_string(AsmError err);

/* Forward declarations for label/fixup structures */
typedef struct AsmLabel AsmLabel;
typedef struct AsmFixup AsmFixup;

struct AsmLabel {
    char     name[64];
    int      offset;       /* Byte offset in current chunk's code */
    AsmLabel *next;
};

struct AsmFixup {
    int      offset;       /* Byte offset in code where the short goes */
    char     label[64];    /* Label name to resolve */
    int      instr_offset; /* Start of the instruction (for relative offset calc) */
    AsmFixup *next;
};

typedef struct {
    Chunk      chunks[LANTERN_FUNCS_MAX];
    int        chunk_count;
    AsmLabel  *labels;     /* Labels for current function */
    AsmFixup  *fixups;     /* Unresolved jump targets */
    AsmError   error;
    char       error_msg[256];
    int        current_line;
} Assembler;

void      asm_init(Assembler *a);
void      asm_free(Assembler *a);
AsmError  asm_parse(Assembler *a, const char *source);
AsmError  asm_parse_file(Assembler *a, const char *path);
Chunk    *asm_get_chunk(Assembler *a, int index);
Chunk    *asm_get_main(Assembler *a);
int       asm_func_count(Assembler *a);

#endif /* LANTERN_H */