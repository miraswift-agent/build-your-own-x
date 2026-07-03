#include "vm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Lantern VM — Stage 1: Core Implementation
 *
 * The fetch-decode-execute cycle is the heartbeat of computation.
 * Everything else is decoration.
 */

/* — Lifecycle — */

VM* vm_new(void) {
    VM *vm = calloc(1, sizeof(VM));
    if (!vm) return NULL;
    vm_reset(vm);
    return vm;
}

void vm_free(VM *vm) {
    free(vm);
}

void vm_reset(VM *vm) {
    memset(vm->memory, 0, VM_MEMORY_SIZE);
    memset(vm->V, 0, VM_NUM_REGS);
    vm->I = 0;
    vm->pc = VM_START_ADDR;
    vm->sp = 0;
    vm->dt = 0;
    vm->st = 0;
    vm->running = false;
    vm->error = VM_OK;

    /* Load fontset into memory at 0x050-0x09F (80 bytes, 5 bytes per digit 0-F) */
    static const uint8_t fontset[80] = {
        0xF0, 0x90, 0x90, 0x90, 0xF0,  /* 0 */
        0x20, 0x60, 0x20, 0x20, 0x70,  /* 1 */
        0xF0, 0x10, 0xF0, 0x80, 0xF0,  /* 2 */
        0xF0, 0x10, 0xF0, 0x10, 0xF0,  /* 3 */
        0x90, 0x90, 0xF0, 0x10, 0x10,  /* 4 */
        0xF0, 0x80, 0xF0, 0x10, 0xF0,  /* 5 */
        0xF0, 0x80, 0xF0, 0x90, 0xF0,  /* 6 */
        0xF0, 0x10, 0x20, 0x40, 0x40,  /* 7 */
        0xF0, 0x90, 0xF0, 0x90, 0xF0,  /* 8 */
        0xF0, 0x90, 0xF0, 0x10, 0xF0,  /* 9 */
        0xF0, 0x90, 0xF0, 0x90, 0x90,  /* A */
        0xE0, 0x90, 0xE0, 0x90, 0xE0,  /* B */
        0xF0, 0x80, 0x80, 0x80, 0xF0,  /* C */
        0xE0, 0x90, 0x90, 0x90, 0xE0,  /* D */
        0xF0, 0x80, 0xF0, 0x80, 0xF0,  /* E */
        0xF0, 0x80, 0xF0, 0x80, 0x80   /* F */
    };
    memcpy(vm->memory + 0x50, fontset, sizeof(fontset));
}

/* — Memory Access — */

uint8_t vm_read_mem(VM *vm, uint16_t addr) {
    if (addr >= VM_MEMORY_SIZE) {
        vm->error = VM_ERR_MEMORY_OUT_OF_BOUNDS;
        return 0;
    }
    return vm->memory[addr];
}

VMError vm_write_mem(VM *vm, uint16_t addr, uint8_t value) {
    if (addr >= VM_MEMORY_SIZE) {
        return VM_ERR_MEMORY_OUT_OF_BOUNDS;
    }
    vm->memory[addr] = value;
    return VM_OK;
}

uint16_t vm_fetch16(VM *vm, uint16_t addr) {
    /* Big-endian: high byte first */
    return ((uint16_t)vm_read_mem(vm, addr) << 8) |
           (uint16_t)vm_read_mem(vm, addr + 1);
}

/* — Program Loading — */

VMError vm_load(VM *vm, const uint8_t *program, size_t size, uint16_t offset) {
    if (offset + size > VM_MEMORY_SIZE) {
        return VM_ERR_MEMORY_OUT_OF_BOUNDS;
    }
    memcpy(vm->memory + offset, program, size);
    return VM_OK;
}

VMError vm_load_default(VM *vm, const uint8_t *program, size_t size) {
    return vm_load(vm, program, size, VM_START_ADDR);
}

/* — Execution — */

void vm_halt(VM *vm) {
    vm->running = false;
}

static uint16_t fetch(VM *vm) {
    if (vm->pc + 1 >= VM_MEMORY_SIZE) {
        vm->error = VM_ERR_PC_OUT_OF_BOUNDS;
        vm->running = false;
        return 0;
    }
    uint16_t opcode = ((uint16_t)vm->memory[vm->pc] << 8) |
                      (uint16_t)vm->memory[vm->pc + 1];
    vm->pc += 2;
    return opcode;
}

VMError vm_step(VM *vm) {
    if (vm->error != VM_OK) return vm->error;

    uint16_t opcode = fetch(vm);
    if (vm->error != VM_OK) return vm->error;

    /* Decode: extract common fields */
    uint8_t  high  = (opcode >> 12) & 0xF;     /* Top nibble */
    uint16_t nnn   = opcode & 0x0FFF;           /* 12-bit address */
    uint8_t  n     = opcode & 0xF;               /* 4-bit nibble */
    uint8_t  x     = (opcode >> 8) & 0xF;        /* 4-bit register index */
    uint8_t  y     = (opcode >> 4) & 0xF;        /* 4-bit register index */
    uint8_t  kk    = opcode & 0xFF;               /* 8-bit constant */

    switch (high) {
    case 0x0:
        if (opcode == 0x0000) {
            /* HALT — stop execution cleanly */
            vm_halt(vm);
        } else if (opcode == 0x00E0) {
            /* CLR — clear display (we track it but don't render) */
        } else if (opcode == 0x00EE) {
            /* RET — return from subroutine */
            if (vm->sp == 0) {
                vm->error = VM_ERR_STACK_UNDERFLOW;
                vm->running = false;
                return vm->error;
            }
            vm->sp--;
            vm->pc = vm->stack[vm->sp];
        } else {
            /* 0nnn — SYS call (ignored) */
        }
        break;

    case 0x1:
        /* JP addr */
        vm->pc = nnn;
        break;

    case 0x2:
        /* CALL addr */
        if (vm->sp >= VM_STACK_SIZE) {
            vm->error = VM_ERR_STACK_OVERFLOW;
            vm->running = false;
            return vm->error;
        }
        vm->stack[vm->sp] = vm->pc;
        vm->sp++;
        vm->pc = nnn;
        break;

    case 0x3:
        /* SE Vx, byte — skip next if Vx == kk */
        if (vm->V[x] == kk) vm->pc += 2;
        break;

    case 0x4:
        /* SNE Vx, byte — skip next if Vx != kk */
        if (vm->V[x] != kk) vm->pc += 2;
        break;

    case 0x5:
        /* SE Vx, Vy — skip next if Vx == Vy */
        if (vm->V[x] == vm->V[y]) vm->pc += 2;
        break;

    case 0x6:
        /* LD Vx, byte */
        vm->V[x] = kk;
        break;

    case 0x7:
        /* ADD Vx, byte (no carry flag) */
        vm->V[x] = (vm->V[x] + kk) & 0xFF;
        break;

    case 0x8: {
        /* 8xy* — arithmetic/logic operations */
        switch (n) {
        case 0x0: vm->V[x] = vm->V[y];                    break;  /* LD Vx, Vy */
        case 0x1: vm->V[x] |= vm->V[y];                   break;  /* OR */
        case 0x2: vm->V[x] &= vm->V[y];                   break;  /* AND */
        case 0x3: vm->V[x] ^= vm->V[y];                   break;  /* XOR */
        case 0x4: {
            /* ADD Vx, Vy — VF = carry */
            uint16_t sum = vm->V[x] + vm->V[y];
            vm->V[0xF] = (sum > 255) ? 1 : 0;
            vm->V[x] = sum & 0xFF;
            break;
        }
        case 0x5: {
            /* SUB Vx, Vy — VF = NOT borrow */
            vm->V[0xF] = (vm->V[x] >= vm->V[y]) ? 1 : 0;
            vm->V[x] = (vm->V[x] - vm->V[y]) & 0xFF;
            break;
        }
        case 0x6: {
            /* SHR Vx — VF = LSB before shift */
            vm->V[0xF] = vm->V[x] & 1;
            vm->V[x] >>= 1;
            break;
        }
        case 0x7: {
            /* SUBN Vx, Vy — VF = NOT borrow */
            vm->V[0xF] = (vm->V[y] >= vm->V[x]) ? 1 : 0;
            vm->V[x] = (vm->V[y] - vm->V[x]) & 0xFF;
            break;
        }
        case 0xE: {
            /* SHL Vx — VF = MSB before shift */
            vm->V[0xF] = (vm->V[x] >> 7) & 1;
            vm->V[x] = (vm->V[x] << 1) & 0xFF;
            break;
        }
        default:
            vm->error = VM_ERR_INVALID_OPCODE;
            vm->running = false;
            return vm->error;
        }
        break;
    }

    case 0x9:
        /* SNE Vx, Vy — skip next if Vx != Vy */
        if (vm->V[x] != vm->V[y]) vm->pc += 2;
        break;

    case 0xA:
        /* LD I, addr */
        vm->I = nnn;
        break;

    case 0xB:
        /* JP V0, addr */
        vm->pc = (nnn + vm->V[0]) & 0xFFF;
        break;

    case 0xC:
        /* RND Vx, byte — Vx = random & kk */
        vm->V[x] = (rand() % 256) & kk;
        break;

    case 0xD: {
        /* DRW Vx, Vy, nibble — draw sprite (display not implemented, just track) */
        /* In our subset VM, we don't render. Just acknowledge the opcode. */
        /* VF = 1 if any pixel was turned off (collision), 0 otherwise */
        vm->V[0xF] = 0;
        break;
    }

    case 0xE:
        /* Key skip instructions (not testing key input in this stage) */
        if (kk == 0x9E) {
            /* SKP Vx — skip if key Vx is pressed (always skip in headless) */
            vm->pc += 2;
        } else if (kk == 0xA1) {
            /* SKNP Vx — skip if key Vx is NOT pressed (never skip in headless) */
        } else {
            vm->error = VM_ERR_INVALID_OPCODE;
            vm->running = false;
            return vm->error;
        }
        break;

    case 0xF:
        /* Fx** — timer and memory operations */
        switch (kk) {
        case 0x07: vm->V[x] = vm->dt;                    break;  /* LD Vx, DT */
        case 0x0A: /* LD Vx, K — wait for key (blocking, set running=false) */
            vm->pc -= 2; /* Re-execute this instruction */
            vm->running = false;
            break;
        case 0x15: vm->dt = vm->V[x];                     break;  /* LD DT, Vx */
        case 0x18: vm->st = vm->V[x];                     break;  /* LD ST, Vx */
        case 0x1E: vm->I = (vm->I + vm->V[x]) & 0xFFF;   break;  /* ADD I, Vx */
        case 0x29: vm->I = vm->V[x] * 5 + 0x50;           break;  /* LD F, Vx */
        case 0x33: {
            /* LD B, Vx — store BCD of Vx at I, I+1, I+2 */
            if (vm->I + 2 >= VM_MEMORY_SIZE) {
                vm->error = VM_ERR_MEMORY_OUT_OF_BOUNDS;
                vm->running = false;
                return vm->error;
            }
            vm->memory[vm->I]     = vm->V[x] / 100;
            vm->memory[vm->I + 1] = (vm->V[x] / 10) % 10;
            vm->memory[vm->I + 2] = vm->V[x] % 10;
            break;
        }
        case 0x55: {
            /* LD [I], Vx — store V0..Vx at I..I+x */
            if (vm->I + x >= VM_MEMORY_SIZE) {
                vm->error = VM_ERR_MEMORY_OUT_OF_BOUNDS;
                vm->running = false;
                return vm->error;
            }
            for (int i = 0; i <= x; i++) {
                vm->memory[vm->I + i] = vm->V[i];
            }
            break;
        }
        case 0x65: {
            /* LD Vx, [I] — load V0..Vx from I..I+x */
            if (vm->I + x >= VM_MEMORY_SIZE) {
                vm->error = VM_ERR_MEMORY_OUT_OF_BOUNDS;
                vm->running = false;
                return vm->error;
            }
            for (int i = 0; i <= x; i++) {
                vm->V[i] = vm->memory[vm->I + i];
            }
            break;
        }
        default:
            vm->error = VM_ERR_INVALID_OPCODE;
            vm->running = false;
            return vm->error;
        }
        break;

    default:
        vm->error = VM_ERR_INVALID_OPCODE;
        vm->running = false;
        return vm->error;
    }

    return VM_OK;
}

VMError vm_run(VM *vm) {
    vm->running = true;
    VMError err;
    while (vm->running) {
        err = vm_step(vm);
        if (err != VM_OK) return err;
    }
    return VM_OK;
}

/* — Error Strings — */

const char* vm_error_string(VMError err) {
    switch (err) {
    case VM_OK:                    return "OK";
    case VM_ERR_STACK_OVERFLOW:    return "Stack overflow";
    case VM_ERR_STACK_UNDERFLOW:   return "Stack underflow";
    case VM_ERR_PC_OUT_OF_BOUNDS: return "PC out of bounds";
    case VM_ERR_INVALID_OPCODE:   return "Invalid opcode";
    case VM_ERR_INVALID_REGISTER: return "Invalid register";
    case VM_ERR_MEMORY_OUT_OF_BOUNDS: return "Memory out of bounds";
    default:                       return "Unknown error";
    }
}