#include "vm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/*
 * Lantern VM — Stage 1 Tests
 *
 * Test the fetch-decode-execute cycle.
 * Every instruction, every edge case, every error path.
 */

#define TEST(name) static void test_##name(void)
#define RUN(name) do { printf("  %-40s ", #name); test_##name(); printf("PASS\n"); } while(0)
#define ASSERT_EQ(a, b) do { if ((a) != (b)) { fprintf(stderr, "FAIL: %s == %s (got %d, expected %d)\n", #a, #b, (int)(a), (int)(b)); exit(1); } } while(0)

/* — Lifecycle Tests — */

TEST(new_and_free) {
    VM *vm = vm_new();
    assert(vm != NULL);
    assert(vm->pc == VM_START_ADDR);
    assert(vm->sp == 0);
    assert(vm->running == false);
    vm_free(vm);
}

TEST(reset) {
    VM *vm = vm_new();
    vm->V[0] = 42;
    vm->V[5] = 99;
    vm->pc = 0x500;
    vm->sp = 3;
    vm_reset(vm);
    ASSERT_EQ(vm->V[0], 0);
    ASSERT_EQ(vm->V[5], 0);
    ASSERT_EQ(vm->pc, VM_START_ADDR);
    ASSERT_EQ(vm->sp, 0);
    vm_free(vm);
}

/* — Memory Access Tests — */

TEST(read_write_memory) {
    VM *vm = vm_new();
    VMError err = vm_write_mem(vm, 0x300, 0xAB);
    ASSERT_EQ(err, VM_OK);
    ASSERT_EQ(vm_read_mem(vm, 0x300), 0xAB);
    vm_free(vm);
}

TEST(memory_out_of_bounds) {
    VM *vm = vm_new();
    VMError err = vm_write_mem(vm, VM_MEMORY_SIZE, 0xFF);
    ASSERT_EQ(err, VM_ERR_MEMORY_OUT_OF_BOUNDS);
    vm_free(vm);
}

TEST(fetch16) {
    VM *vm = vm_new();
    vm_write_mem(vm, 0x300, 0x12);
    vm_write_mem(vm, 0x301, 0x34);
    ASSERT_EQ(vm_fetch16(vm, 0x300), 0x1234);
    vm_free(vm);
}

/* — Program Loading Tests — */

TEST(load_program) {
    VM *vm = vm_new();
    uint8_t program[] = { 0x60, 0x42,  /* LD V0, 0x42 */
                           0x61, 0x55 }; /* LD V1, 0x55 */
    VMError err = vm_load_default(vm, program, sizeof(program));
    ASSERT_EQ(err, VM_OK);
    ASSERT_EQ(vm_read_mem(vm, VM_START_ADDR), 0x60);
    ASSERT_EQ(vm_read_mem(vm, VM_START_ADDR + 1), 0x42);
    vm_free(vm);
}

/* — Instruction Tests — */

TEST(ld_vx_byte) {
    VM *vm = vm_new();
    uint8_t program[] = {
        0x60, 0x42,  /* LD V0, 0x42 */
        0x61, 0xFF,  /* LD V1, 0xFF */
        0x6A, 0x00   /* LD VA, 0x00 */
    };
    vm_load_default(vm, program, sizeof(program));
    vm_run(vm);
    ASSERT_EQ(vm->V[0], 0x42);
    ASSERT_EQ(vm->V[1], 0xFF);
    ASSERT_EQ(vm->V[0xA], 0x00);
    vm_free(vm);
}

TEST(ld_vx_vy) {
    VM *vm = vm_new();
    uint8_t program[] = {
        0x60, 0x42,  /* LD V0, 0x42 */
        0x81, 0x00   /* LD V1, V0 */
    };
    vm_load_default(vm, program, sizeof(program));
    vm_run(vm);
    ASSERT_EQ(vm->V[1], 0x42);
    vm_free(vm);
}

TEST(add_vx_byte) {
    VM *vm = vm_new();
    uint8_t program[] = {
        0x60, 0x10,  /* LD V0, 0x10 */
        0x70, 0x20   /* ADD V0, 0x20 */
    };
    vm_load_default(vm, program, sizeof(program));
    vm_run(vm);
    ASSERT_EQ(vm->V[0], 0x30);
    vm_free(vm);
}

TEST(add_vx_byte_overflow) {
    VM *vm = vm_new();
    uint8_t program[] = {
        0x60, 0xFF,  /* LD V0, 0xFF */
        0x70, 0x02   /* ADD V0, 0x02 — wraps to 0x01, no carry flag for 7xkk */
    };
    vm_load_default(vm, program, sizeof(program));
    vm_run(vm);
    ASSERT_EQ(vm->V[0], 0x01);  /* Wraps within byte, no VF set for 7xkk */
    vm_free(vm);
}

TEST(add_vx_vy_with_carry) {
    VM *vm = vm_new();
    uint8_t program[] = {
        0x60, 0xFF,  /* LD V0, 0xFF */
        0x61, 0x01,  /* LD V1, 0x01 */
        0x80, 0x14   /* ADD V0, V1 — 0xFF + 0x01 = 0x100, VF=1, V0=0 */
    };
    vm_load_default(vm, program, sizeof(program));
    vm_run(vm);
    ASSERT_EQ(vm->V[0], 0x00);
    ASSERT_EQ(vm->V[0xF], 1);
    vm_free(vm);
}

TEST(sub_vx_vy) {
    VM *vm = vm_new();
    uint8_t program[] = {
        0x60, 0x05,  /* LD V0, 0x05 */
        0x61, 0x03,  /* LD V1, 0x03 */
        0x80, 0x15   /* SUB V0, V1 — 5-3=2, VF=1 (no borrow) */
    };
    vm_load_default(vm, program, sizeof(program));
    vm_run(vm);
    ASSERT_EQ(vm->V[0], 0x02);
    ASSERT_EQ(vm->V[0xF], 1);
    vm_free(vm);
}

TEST(sub_vx_vy_borrow) {
    VM *vm = vm_new();
    uint8_t program[] = {
        0x60, 0x03,  /* LD V0, 0x03 */
        0x61, 0x05,  /* LD V1, 0x05 */
        0x80, 0x15   /* SUB V0, V1 — 3-5 wraps, VF=0 (borrow) */
    };
    vm_load_default(vm, program, sizeof(program));
    vm_run(vm);
    ASSERT_EQ(vm->V[0], (0x03 - 0x05) & 0xFF);
    ASSERT_EQ(vm->V[0xF], 0);
    vm_free(vm);
}

TEST(and_or_xor) {
    VM *vm = vm_new();
    uint8_t program[] = {
        0x60, 0xFF,  /* LD V0, 0xFF */
        0x61, 0x0F,  /* LD V1, 0x0F */
        0x82, 0x00,  /* LD V2, V0 (V2=0xFF) — opcode 8200: x=2,y=0,n=0 */
        0x82, 0x11,  /* OR V2, V1 — V2 = 0xFF | 0x0F = 0xFF — opcode 8211: x=2,y=1,n=1 */
        0x83, 0x00,  /* LD V3, V0 (V3=0xFF) — opcode 8300: x=3,y=0,n=0 */
        0x83, 0x12,  /* AND V3, V1 — V3 = 0xFF & 0x0F = 0x0F — opcode 8312: x=3,y=1,n=2 */
        0x84, 0x00,  /* LD V4, V0 (V4=0xFF) — opcode 8400: x=4,y=0,n=0 */
        0x84, 0x13   /* XOR V4, V1 — V4 = 0xFF ^ 0x0F = 0xF0 — opcode 8413: x=4,y=1,n=3 */
    };
    vm_load_default(vm, program, sizeof(program));
    vm_run(vm);
    ASSERT_EQ(vm->V[2], 0xFF);
    ASSERT_EQ(vm->V[3], 0x0F);
    ASSERT_EQ(vm->V[4], 0xF0);
    vm_free(vm);
}

TEST(shr_shl) {
    VM *vm = vm_new();
    uint8_t program[] = {
        0x60, 0x0D,  /* LD V0, 0x0D (1101) */
        0x80, 0x06,  /* SHR V0 — V0=0x06, VF=1 (LSB was 1) */
        0x61, 0x80,  /* LD V1, 0x80 */
        0x81, 0x0E   /* SHL V1 — V1=0x00, VF=1 (MSB was 1) */
    };
    vm_load_default(vm, program, sizeof(program));
    vm_run(vm);
    ASSERT_EQ(vm->V[0], 0x06);
    ASSERT_EQ(vm->V[1], 0x00);
    vm_free(vm);
}

/* — Control Flow Tests — */

TEST(jp_addr) {
    VM *vm = vm_new();
    uint8_t program[] = {
        0x60, 0x42,  /* LD V0, 0x42 */
        0x12, 0x08   /* JP 0x208 — jump past next instruction */
    };
    vm_load_default(vm, program, sizeof(program));
    vm_run(vm);
    ASSERT_EQ(vm->V[0], 0x42);
    vm_free(vm);
}

TEST(call_ret) {
    VM *vm = vm_new();
    /* Call subroutine at 0x300 that sets V3 = 0x77, then return */
    uint8_t subroutine[] = {
        0x63, 0x77,  /* LD V3, 0x77 */
        0x00, 0xEE   /* RET */
    };
    uint8_t program[] = {
        0x23, 0x00,  /* CALL 0x300 */
        0x60, 0x42   /* LD V0, 0x42 (executed after return) */
    };
    vm_load(vm, subroutine, sizeof(subroutine), 0x300);
    vm_load_default(vm, program, sizeof(program));
    vm_run(vm);
    ASSERT_EQ(vm->V[3], 0x77);
    ASSERT_EQ(vm->V[0], 0x42);
    ASSERT_EQ(vm->sp, 0);
    vm_free(vm);
}

TEST(se_vx_byte_skip) {
    VM *vm = vm_new();
    uint8_t program[] = {
        0x60, 0x42,  /* LD V0, 0x42 */
        0x30, 0x42,  /* SE V0, 0x42 — skip next (V0 == 0x42) */
        0x61, 0x01,  /* LD V1, 0x01 (skipped) */
        0x61, 0x02   /* LD V1, 0x02 (executed) */
    };
    vm_load_default(vm, program, sizeof(program));
    vm_run(vm);
    ASSERT_EQ(vm->V[1], 0x02);
    vm_free(vm);
}

TEST(se_vx_byte_no_skip) {
    VM *vm = vm_new();
    uint8_t program[] = {
        0x60, 0x42,  /* LD V0, 0x42 */
        0x30, 0x43,  /* SE V0, 0x43 — no skip (V0 != 0x43) */
        0x61, 0x01   /* LD V1, 0x01 (executed) */
    };
    vm_load_default(vm, program, sizeof(program));
    vm_run(vm);
    ASSERT_EQ(vm->V[1], 0x01);
    vm_free(vm);
}

TEST(sne_vx_vy) {
    VM *vm = vm_new();
    uint8_t program[] = {
        0x60, 0x42,  /* LD V0, 0x42 */
        0x61, 0x42,  /* LD V1, 0x42 */
        0x90, 0x10,  /* SNE V0, V1 — no skip (equal) */
        0x62, 0x01,  /* LD V2, 0x01 (executed) */
        0x60, 0x42,  /* LD V0, 0x42 */
        0x61, 0x43,  /* LD V1, 0x43 */
        0x90, 0x10,  /* SNE V0, V1 — skip (not equal) */
        0x63, 0x01,  /* LD V3, 0x01 (skipped) */
        0x63, 0x02   /* LD V3, 0x02 (executed) */
    };
    vm_load_default(vm, program, sizeof(program));
    vm_run(vm);
    ASSERT_EQ(vm->V[2], 0x01);
    ASSERT_EQ(vm->V[3], 0x02);
    vm_free(vm);
}

/* — Memory Operation Tests — */

TEST(ld_i_addr) {
    VM *vm = vm_new();
    uint8_t program[] = {
        0xA3, 0x00   /* LD I, 0x300 */
    };
    vm_load_default(vm, program, sizeof(program));
    vm_run(vm);
    ASSERT_EQ(vm->I, 0x300);
    vm_free(vm);
}

TEST(ld_store_load_regs) {
    VM *vm = vm_new();
    /* Store V0-V2 at I, then load them back */
    uint8_t program[] = {
        0x60, 0x0A,  /* LD V0, 0x0A */
        0x61, 0x0B,  /* LD V1, 0x0B */
        0x62, 0x0C,  /* LD V2, 0x0C */
        0xA3, 0x00,  /* LD I, 0x300 */
        0xF2, 0x55,  /* LD [I], V0-V2 — store 3 regs */
        /* Now clear V0-V2 and reload */
        0x60, 0x00,  /* LD V0, 0x00 */
        0x61, 0x00,  /* LD V1, 0x00 */
        0x62, 0x00,  /* LD V2, 0x00 */
        0xA3, 0x00,  /* LD I, 0x300 */
        0xF2, 0x65   /* LD V0-V2, [I] — load 3 regs */
    };
    vm_load_default(vm, program, sizeof(program));
    vm_run(vm);
    ASSERT_EQ(vm->V[0], 0x0A);
    ASSERT_EQ(vm->V[1], 0x0B);
    ASSERT_EQ(vm->V[2], 0x0C);
    vm_free(vm);
}

TEST(bcd) {
    VM *vm = vm_new();
    uint8_t program[] = {
        0x60, 0x7B,  /* LD V0, 123 (0x7B) */
        0xA3, 0x00,  /* LD I, 0x300 */
        0xF0, 0x33   /* LD B, V0 — BCD: 1, 2, 3 */
    };
    vm_load_default(vm, program, sizeof(program));
    vm_run(vm);
    ASSERT_EQ(vm_read_mem(vm, 0x300), 1);
    ASSERT_EQ(vm_read_mem(vm, 0x301), 2);
    ASSERT_EQ(vm_read_mem(vm, 0x302), 3);
    vm_free(vm);
}

/* — Error Handling Tests — */

TEST(stack_overflow) {
    VM *vm = vm_new();
    /* Push more than VM_STACK_SIZE times */
    uint8_t program[VM_STACK_SIZE * 2 + 2];
    for (int i = 0; i < VM_STACK_SIZE + 1; i++) {
        program[i * 2]     = 0x22;  /* CALL 0x300 */
        program[i * 2 + 1] = 0x00;
    }
    vm_load(vm, (uint8_t[]){0x63, 0x77, 0x00, 0xEE}, 4, 0x300); /* Subroutine */
    vm_load_default(vm, program, (VM_STACK_SIZE + 1) * 2);
    VMError err = vm_run(vm);
    ASSERT_EQ(err, VM_ERR_STACK_OVERFLOW);
    vm_free(vm);
}

TEST(stack_underflow) {
    VM *vm = vm_new();
    uint8_t program[] = {
        0x00, 0xEE   /* RET — stack underflow */
    };
    vm_load_default(vm, program, sizeof(program));
    VMError err = vm_run(vm);
    ASSERT_EQ(err, VM_ERR_STACK_UNDERFLOW);
    vm_free(vm);
}

TEST(invalid_opcode) {
    VM *vm = vm_new();
    /* Opcode 0x8008 — 8xy8 is not a valid arithmetic instruction */
    uint8_t program[] = {
        0x80, 0x08   /* 0x8008: undefined 8xy8 opcode */
    };
    vm_load_default(vm, program, sizeof(program));
    VMError err = vm_run(vm);
    ASSERT_EQ(err, VM_ERR_INVALID_OPCODE);
    vm_free(vm);
}

/* — Fibonacci Test (Integration) — */

TEST(fibonacci_10) {
    /* Compute F(10) = 55 using Chip-8 instructions
     * V0 = a (starts at 0), V1 = b (starts at 1)
     * V2 = temp, V3 = counter (counts down)
     * Each iteration: temp=b, b=a+b, a=temp
     * After 9 iterations: V1 = F(10) = 55
     * Trace: (0,1)->(1,1)->(1,2)->(2,3)->(3,5)->(5,8)->(8,13)->(13,21)->(21,34)->(34,55) */
    VM *vm = vm_new();
    uint8_t fib_program[] = {
        0x60, 0x00,  /* 0x200: LD V0, 0       (a = 0) */
        0x61, 0x01,  /* 0x202: LD V1, 1       (b = 1) */
        0x63, 0x09,  /* 0x204: LD V3, 9       (counter = 9) */
        /* loop at 0x206: */
        0x82, 0x10,  /* 0x206: LD V2, V1      (temp = b) */
        0x81, 0x04,  /* 0x208: ADD V1, V0      (b = b + a) */
        0x80, 0x20,  /* 0x20A: LD V0, V2       (a = old b) */
        0x73, 0xFF,  /* 0x20C: ADD V3, 0xFF   (counter--) */
        0x33, 0x00,  /* 0x20E: SE V3, 0        (skip if counter == 0) */
        0x12, 0x06   /* 0x210: JP 0x206        (loop back) */
        /* 0x212: done, V1 = F(10) = 55, then hits 0x0000 HALT */
    };
    vm_load_default(vm, fib_program, sizeof(fib_program));
    vm_run(vm);
    ASSERT_EQ(vm->V[1], 55);  /* F(10) = 55 */
    vm_free(vm);
}

/* — Jump V0 Test — */

TEST(jp_v0) {
    VM *vm = vm_new();
    /* JP V0, 0x200 + V0 offset */
    uint8_t program[] = {
        0x60, 0x04,  /* LD V0, 4 */
        0xB2, 0x06   /* JP V0 + 0x206 — but V0=4, so jump to 0x206+4=0x20A */
    };
    /* Put a LD V1, 0x77 at 0x20A */
    vm_load_default(vm, program, sizeof(program));
    uint8_t target[] = { 0x61, 0x77 };
    vm_load(vm, target, sizeof(target), 0x20A);
    vm_run(vm);
    ASSERT_EQ(vm->V[1], 0x77);
    vm_free(vm);
}

/* — Nested Calls Test — */

TEST(nested_calls) {
    VM *vm = vm_new();
    /* Call sub1 at 0x300, which calls sub2 at 0x400 */
    uint8_t sub2[] = {
        0x63, 0x42,  /* LD V3, 0x42 */
        0x00, 0xEE   /* RET */
    };
    uint8_t sub1[] = {
        0x24, 0x00,  /* CALL 0x400 */
        0x62, 0x77,  /* LD V2, 0x77 */
        0x00, 0xEE   /* RET */
    };
    uint8_t main_prog[] = {
        0x23, 0x00,  /* CALL 0x300 */
        0x60, 0x99   /* LD V0, 0x99 (executed after both returns) */
    };
    vm_load(vm, sub2, sizeof(sub2), 0x400);
    vm_load(vm, sub1, sizeof(sub1), 0x300);
    vm_load_default(vm, main_prog, sizeof(main_prog));
    vm_run(vm);
    ASSERT_EQ(vm->V[3], 0x42);  /* Set in sub2 */
    ASSERT_EQ(vm->V[2], 0x77);  /* Set in sub1 after returning from sub2 */
    ASSERT_EQ(vm->V[0], 0x99);  /* Set in main after returning from sub1 */
    ASSERT_EQ(vm->sp, 0);       /* Stack should be empty */
    vm_free(vm);
}

/* — Fontset Test — */

TEST(fontset_loaded) {
    VM *vm = vm_new();
    /* Fontset for '0' should be at 0x50 */
    ASSERT_EQ(vm_read_mem(vm, 0x50), 0xF0);
    ASSERT_EQ(vm_read_mem(vm, 0x51), 0x90);
    vm_free(vm);
}

TEST(font_character_addr) {
    VM *vm = vm_new();
    uint8_t program[] = {
        0x60, 0x03,  /* LD V0, 3 */
        0xF0, 0x29   /* LD F, V0 — I = V0 * 5 + 0x50 = 3*5+0x50 = 0x5F */
    };
    vm_load_default(vm, program, sizeof(program));
    vm_run(vm);
    ASSERT_EQ(vm->I, 0x5F);
    vm_free(vm);
}

/* — Stress Tests — */

TEST(deep_nesting) {
    /* Call 15 levels deep (stack size is 16, so 15 is max safe depth) */
    VM *vm = vm_new();
    /* Create 15 nested subroutines at 0x300, 0x310, 0x320, ..., 0x3E0 */
    for (int i = 0; i < 15; i++) {
        uint16_t addr = 0x300 + i * 0x10;
        uint16_t next_addr = 0x300 + (i + 1) * 0x10;
        if (i < 14) {
            uint8_t sub_fixed[] = {
                0x20 | ((uint8_t)(next_addr >> 8) & 0x0F),  /* CALL next_addr high byte */
                next_addr & 0xFF,                    /* CALL next_addr low byte */
                0x00, 0xEE                           /* RET */
            };
            vm_load(vm, sub_fixed, sizeof(sub_fixed), addr);
        } else {
            /* Deepest level — just set V0 */
            uint8_t sub[] = {
                0x60, 0x37,  /* LD V0, 0x37 */
                0x00, 0xEE   /* RET */
            };
            vm_load(vm, sub, sizeof(sub), addr);
        }
    }
    /* Main program calls first subroutine */
    uint8_t main_prog[] = {
        0x23, 0x00,  /* CALL 0x300 */
    };
    vm_load_default(vm, main_prog, sizeof(main_prog));
    vm_run(vm);
    ASSERT_EQ(vm->V[0], 0x37);
    ASSERT_EQ(vm->sp, 0);  /* All returns unwound */
    vm_free(vm);
}

TEST(stack_overflow_boundary) {
    /* Exactly 16 calls should overflow (stack size = 16, sp goes 0..15) */
    VM *vm = vm_new();
    /* Create a subroutine at 0x300 that calls itself */
    uint8_t recurse[] = {
        0x23, 0x00,  /* CALL 0x300 (call self) */
        0x00, 0xEE   /* RET (never reached) */
    };
    vm_load(vm, recurse, sizeof(recurse), 0x300);
    uint8_t main_prog[] = {
        0x23, 0x00   /* CALL 0x300 */
    };
    vm_load_default(vm, main_prog, sizeof(main_prog));
    VMError err = vm_run(vm);
    ASSERT_EQ(err, VM_ERR_STACK_OVERFLOW);
    vm_free(vm);
}

TEST(register_independence) {
    /* Verify that operations on one register don't clobber others */
    VM *vm = vm_new();
    uint8_t program[] = {
        0x60, 0x11,  /* LD V0, 0x11 */
        0x61, 0x22,  /* LD V1, 0x22 */
        0x62, 0x33,  /* LD V2, 0x33 */
        0x63, 0x44,  /* LD V3, 0x44 */
        0x64, 0x55,  /* LD V4, 0x55 */
        0x65, 0x66,  /* LD V5, 0x66 */
        0x66, 0x77,  /* LD V6, 0x77 */
        0x67, 0x88,  /* LD V7, 0x88 */
        0x68, 0x99,  /* LD V8, 0x99 */
        0x69, 0xAA,  /* LD V9, 0xAA */
        0x6A, 0xBB,  /* LD VA, 0xBB */
        0x6B, 0xCC,  /* LD VB, 0xCC */
        0x6C, 0xDD,  /* LD VC, 0xDD */
        0x6D, 0xEE,  /* LD VD, 0xEE */
        0x6E, 0xF0,  /* LD VE, 0xF0 */
        /* Don't set VF (0xF) — it's the flag register */
    };
    vm_load_default(vm, program, sizeof(program));
    vm_run(vm);
    /* All registers should hold their values */
    ASSERT_EQ(vm->V[0], 0x11);
    ASSERT_EQ(vm->V[1], 0x22);
    ASSERT_EQ(vm->V[2], 0x33);
    ASSERT_EQ(vm->V[3], 0x44);
    ASSERT_EQ(vm->V[4], 0x55);
    ASSERT_EQ(vm->V[5], 0x66);
    ASSERT_EQ(vm->V[6], 0x77);
    ASSERT_EQ(vm->V[7], 0x88);
    ASSERT_EQ(vm->V[8], 0x99);
    ASSERT_EQ(vm->V[9], 0xAA);
    ASSERT_EQ(vm->V[0xA], 0xBB);
    ASSERT_EQ(vm->V[0xB], 0xCC);
    ASSERT_EQ(vm->V[0xC], 0xDD);
    ASSERT_EQ(vm->V[0xD], 0xEE);
    ASSERT_EQ(vm->V[0xE], 0xF0);
    vm_free(vm);
}

TEST(vf_clobber) {
    /* ADD Vx, Vy clobbers VF with carry flag */
    VM *vm = vm_new();
    uint8_t program[] = {
        0x6F, 0x42,  /* LD VF, 0x42 */
        0x60, 0xFF,  /* LD V0, 0xFF */
        0x61, 0x01,  /* LD V1, 0x01 */
        0x80, 0x14   /* ADD V0, V1 — carry sets VF */
    };
    vm_load_default(vm, program, sizeof(program));
    vm_run(vm);
    ASSERT_EQ(vm->V[0xF], 1);  /* VF clobbered by carry */
    ASSERT_EQ(vm->V[0], 0x00);  /* 0xFF + 0x01 = 0x00 with carry */
    vm_free(vm);
}

TEST(memory_edge_program_at_end) {
    /* Load a program at the very end of memory */
    VM *vm = vm_new();
    uint8_t program[] = {
        0x60, 0x77   /* LD V0, 0x77 */
    };
    /* Load at 0xFFE so program spans 0xFFE-0xFFF (last 2 bytes) */
    VMError err = vm_load(vm, program, sizeof(program), 0xFFE);
    ASSERT_EQ(err, VM_OK);
    /* Set PC to start there */
    vm->pc = 0xFFE;
    vm->running = true;
    vm_step(vm);  /* Execute LD V0, 0x77 */
    ASSERT_EQ(vm->V[0], 0x77);
    /* Next step should fail — PC at 0x1000, out of bounds */
    vm->running = true;
    err = vm_step(vm);
    ASSERT_EQ(err, VM_ERR_PC_OUT_OF_BOUNDS);
    vm_free(vm);
}

TEST(pc_out_of_bounds) {
    /* Set PC past memory and try to step */
    VM *vm = vm_new();
    vm->pc = VM_MEMORY_SIZE;  /* Past end */
    vm->running = true;
    VMError err = vm_step(vm);
    ASSERT_EQ(err, VM_ERR_PC_OUT_OF_BOUNDS);
    vm_free(vm);
}

TEST(bcd_edge_cases) {
    /* BCD of 0, 99, and 255 */
    VM *vm = vm_new();
    uint8_t program[] = {
        0x60, 0x00,  /* LD V0, 0 */
        0xA3, 0x00,  /* LD I, 0x300 */
        0xF0, 0x33,  /* LD B, V0 — BCD of 0 */
        0x60, 0x63,  /* LD V0, 99 */
        0xA3, 0x10,  /* LD I, 0x310 */
        0xF0, 0x33,  /* LD B, V0 — BCD of 99 */
        0x60, 0xFF,  /* LD V0, 255 */
        0xA3, 0x20,  /* LD I, 0x320 */
        0xF0, 0x33   /* LD B, V0 — BCD of 255 (2,5,5) */
    };
    vm_load_default(vm, program, sizeof(program));
    vm_run(vm);
    /* BCD of 0 */
    ASSERT_EQ(vm_read_mem(vm, 0x300), 0);
    ASSERT_EQ(vm_read_mem(vm, 0x301), 0);
    ASSERT_EQ(vm_read_mem(vm, 0x302), 0);
    /* BCD of 99 */
    ASSERT_EQ(vm_read_mem(vm, 0x310), 0);
    ASSERT_EQ(vm_read_mem(vm, 0x311), 9);
    ASSERT_EQ(vm_read_mem(vm, 0x312), 9);
    /* BCD of 255 */
    ASSERT_EQ(vm_read_mem(vm, 0x320), 2);
    ASSERT_EQ(vm_read_mem(vm, 0x321), 5);
    ASSERT_EQ(vm_read_mem(vm, 0x322), 5);
    vm_free(vm);
}

TEST(add_i_vx_wrap) {
    /* ADD I, Vx should wrap within 12-bit address space */
    VM *vm = vm_new();
    uint8_t program[] = {
        0xA0, 0x00,  /* LD I, 0x000 */
        0x60, 0x05,  /* LD V0, 5 */
        0xF0, 0x1E   /* ADD I, V0 */
    };
    vm_load_default(vm, program, sizeof(program));
    vm_run(vm);
    ASSERT_EQ(vm->I, 0x005);
    vm_free(vm);
}

/* — Main — */

int main(void) {
    printf("Lantern VM — Stage 1 Tests\n");
    printf("==========================\n\n");

    printf("Lifecycle:\n");
    RUN(new_and_free);
    RUN(reset);

    printf("\nMemory:\n");
    RUN(read_write_memory);
    RUN(memory_out_of_bounds);
    RUN(fetch16);

    printf("\nProgram Loading:\n");
    RUN(load_program);

    printf("\nArithmetic:\n");
    RUN(ld_vx_byte);
    RUN(ld_vx_vy);
    RUN(add_vx_byte);
    RUN(add_vx_byte_overflow);
    RUN(add_vx_vy_with_carry);
    RUN(sub_vx_vy);
    RUN(sub_vx_vy_borrow);
    RUN(and_or_xor);
    RUN(shr_shl);

    printf("\nControl Flow:\n");
    RUN(jp_addr);
    RUN(call_ret);
    RUN(se_vx_byte_skip);
    RUN(se_vx_byte_no_skip);
    RUN(sne_vx_vy);
    RUN(jp_v0);
    RUN(nested_calls);

    printf("\nMemory Operations:\n");
    RUN(ld_i_addr);
    RUN(ld_store_load_regs);
    RUN(bcd);

    printf("\nError Handling:\n");
    RUN(stack_overflow);
    RUN(stack_underflow);
    RUN(invalid_opcode);

    printf("\nIntegration:\n");
    RUN(fibonacci_10);
    RUN(fontset_loaded);
    RUN(font_character_addr);

    printf("\nStress:\n");
    RUN(deep_nesting);
    RUN(stack_overflow_boundary);
    RUN(register_independence);
    RUN(vf_clobber);
    RUN(memory_edge_program_at_end);
    RUN(pc_out_of_bounds);
    RUN(bcd_edge_cases);
    RUN(add_i_vx_wrap);

    printf("\n✓ All tests passed!\n");
    return 0;
}