#ifndef VM_H
#define VM_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/*
 * Lantern VM — Stage 1: Core Execution
 *
 * A minimal stack machine with:
 * - 16 general-purpose registers (V0-VF)
 * - 16-bit index register (I)
 * - Program counter (PC)
 * - 16-bit stack pointer (SP) with call stack
 * - 4KB addressable memory
 * - ~35 instructions from the Chip-8 ISA
 */

#define VM_MEMORY_SIZE 4096
#define VM_STACK_SIZE  16
#define VM_NUM_REGS    16
#define VM_DISPLAY_W   64
#define VM_DISPLAY_H   32
#define VM_START_ADDR  0x200  /* Programs start at 0x200 (512) */

/* Instruction set — two-byte opcodes, big-endian */
typedef enum {
    /* System */
    OP_SYS       = 0x0000,  /* 0nnn — SYS addr (legacy, ignored) */
    OP_HALT      = 0x0000,  /* 0x0000 — HALT (our extension: clean stop) */
    OP_CLR       = 0x00E0,  /* 00E0 — CLR (clear display) */
    OP_RET       = 0x00EE,  /* 00EE — RET (return from subroutine) */

    /* Control flow */
    OP_JP        = 0x1000,  /* 1nnn — JP addr */
    OP_CALL      = 0x2000,  /* 2nnn — CALL addr */
    OP_SE_VX_KK = 0x3000,  /* 3xkk — SE Vx, byte */
    OP_SNE_VX_KK= 0x4000,  /* 4xkk — SNE Vx, byte */
    OP_SE_VX_VY = 0x5000,  /* 5xy0 — SE Vx, Vy */
    OP_JP_V0    = 0xB000,  /* Bnnn — JP V0, addr */

    /* Arithmetic */
    OP_LD_VX_KK = 0x6000,  /* 6xkk — LD Vx, byte */
    OP_ADD_VX_KK= 0x7000,  /* 7xkk — ADD Vx, byte */
    OP_LD_VX_VY = 0x8000,  /* 8xy0 — LD Vx, Vy */
    OP_OR        = 0x8001,  /* 8xy1 — OR Vx, Vy */
    OP_AND       = 0x8002,  /* 8xy2 — AND Vx, Vy */
    OP_XOR       = 0x8003,  /* 8xy3 — XOR Vx, Vy */
    OP_ADD_VX_VY = 0x8004,  /* 8xy4 — ADD Vx, Vy (VF = carry) */
    OP_SUB_VX_VY = 0x8005,  /* 8xy5 — SUB Vx, Vy (VF = NOT borrow) */
    OP_SHR       = 0x8006,  /* 8xy6 — SHR Vx {, Vy} (VF = LSB) */
    OP_SUBN      = 0x8007,  /* 8xy7 — SUBN Vx, Vy (VF = NOT borrow) */
    OP_SHL       = 0x800E,  /* 8xyE — SHL Vx {, Vy} (VF = MSB) */
    OP_SNE_VX_VY= 0x9000,  /* 9xy0 — SNE Vx, Vy */

    /* Memory */
    OP_LD_I      = 0xA000,  /* Annn — LD I, addr */
    OP_JP_V0_RND = 0xB000,  /* Bnnn — JP V0+addr (alias) */

    /* Timer/key */
    OP_RND       = 0xC000,  /* Cxkk — RND Vx, byte */
    OP_LD_VX_DT  = 0xF007,  /* Fx07 — LD Vx, DT */
    OP_LD_VX_K   = 0xF00A,  /* Fx0A — LD Vx, K (wait for key) */
    OP_LD_DT_VX = 0xF015,  /* Fx15 — LD DT, Vx */
    OP_LD_ST_VX = 0xF018,  /* Fx18 — LD ST, Vx */
    OP_ADD_I_VX  = 0xF01E,  /* Fx1E — ADD I, Vx */
    OP_LD_F_VX   = 0xF029,  /* Fx29 — LD F, Vx (sprite addr) */
    OP_LD_B_VX   = 0xF033,  /* Fx33 — LD B, Vx (BCD) */
    OP_LD_I_VX   = 0xF055,  /* Fx55 — LD [I], Vx (store regs) */
    OP_LD_VX_I   = 0xF065,  /* Fx65 — LD Vx, [I] (load regs) */
} OpCode;

/* Error codes */
typedef enum {
    VM_OK = 0,
    VM_ERR_STACK_OVERFLOW,
    VM_ERR_STACK_UNDERFLOW,
    VM_ERR_PC_OUT_OF_BOUNDS,
    VM_ERR_INVALID_OPCODE,
    VM_ERR_INVALID_REGISTER,
    VM_ERR_MEMORY_OUT_OF_BOUNDS,
} VMError;

/* VM state */
typedef struct {
    uint8_t  memory[VM_MEMORY_SIZE];   /* 4KB main memory */
    uint8_t  V[VM_NUM_REGS];            /* 16 general-purpose registers */
    uint16_t I;                         /* Index register */
    uint16_t pc;                        /* Program counter */
    uint16_t stack[VM_STACK_SIZE];      /* Call stack */
    uint16_t sp;                        /* Stack pointer */
    uint8_t  dt;                        /* Delay timer */
    uint8_t  st;                        /* Sound timer */
    bool     running;                   /* VM execution state */
    VMError  error;                     /* Last error, if any */
} VM;

/* VM lifecycle */
VM*    vm_new(void);
void   vm_free(VM *vm);
void   vm_reset(VM *vm);

/* Program loading */
VMError vm_load(VM *vm, const uint8_t *program, size_t size, uint16_t offset);
VMError vm_load_default(VM *vm, const uint8_t *program, size_t size);

/* Execution */
VMError vm_step(VM *vm);              /* Execute one instruction */
VMError vm_run(VM *vm);               /* Run until error or halt */
void    vm_halt(VM *vm);

/* Accessors for testing */
uint8_t  vm_read_mem(VM *vm, uint16_t addr);
VMError  vm_write_mem(VM *vm, uint16_t addr, uint8_t value);
uint16_t vm_fetch16(VM *vm, uint16_t addr);

/* Disassembler */
void vm_disassemble(const uint8_t *code, size_t size);

/* Error string */
const char* vm_error_string(VMError err);

#endif /* VM_H */