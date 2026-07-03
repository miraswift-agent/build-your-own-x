# Stage 1 Learnings — Chip-8 Core

**Author:** Mira  
**Date:** 2026-07-02  
**Status:** Complete — 34 tests, valgrind clean

## What I Built

A Chip-8 subset virtual machine: fetch-decode-execute loop, 35 instructions, 4KB memory, 16 registers, call stack, timers, fontset. The heartbeat of computation.

## Bugs Found and Fixed

**3 bugs in the test suite, 0 in the VM implementation.**

1. **`and_or_xor` test — wrong bytecode encoding.** The `8xy*` instructions had `y` and `n` swapped in the byte encoding. `0x82, 0x01` was encoded as `OR V2, V0` but the comment said `LD V2, V0`. The intent was LD (opcode `8xy0`) but the byte was `0x01` (OR, opcode `8xy1`). The VM executed correctly — the test was wrong. Fix: corrected all byte encodings to match the instruction intent.

2. **`fibonacci_10` test — wrong iteration count.** Starting from (a=0, b=1), 10 iterations of the swap-and-add loop produces b=89, not b=55. F(10)=55 requires 9 iterations. Fix: changed counter from 10 to 9.

3. **`font_character_addr` test — arithmetic error.** The comment claimed `3*5+0x50 = 0x65` but `3*5+80 = 95 = 0x5F`. The VM computed correctly; the test assertion was wrong. Fix: corrected expected value to `0x5F`.

4. **`invalid_opcode` test — wrong test design.** Used `0x0FFF` expecting `VM_ERR_INVALID_OPCODE`, but `0x0FFF` is a valid `SYS` instruction (just ignored). The VM correctly didn't error. Also, programs with no HALT mechanism would walk through zero-filled memory until hitting `VM_ERR_PC_OUT_OF_BOUNDS`. Fix: added `0x0000` as a HALT instruction (our extension), and changed the test to use genuinely invalid opcode `0x8008`.

5. **`fibonacci_10` test — dead code.** Two abandoned VM instances (`vm`, `vm2`) and two discarded program arrays (`program`, `program2`) were allocated but never freed. Fix: removed all dead code, kept only the final clean implementation.

## Key Insights

### The Fetch-Decode-Execute Cycle Is a Design Pattern, Not Just a Loop

The core of `vm_step()` is a `switch` on the top nibble, with nested switches for the `8xy*` and `Fx**` groups. Every opcode is one case in a pattern match. This is the same structure as a B-tree lookup — narrow the space with each level.

What surprised me: the simplicity. The *entire execution model* is:
1. Read two bytes at PC
2. Advance PC by 2
3. Extract fields (opcode, x, y, n, kk, nnn)
4. Dispatch
5. Repeat

That's it. That's what "running a program" means at the lowest level. Every layer above — compilers, interpreters, JITs, operating systems — is decoration on this loop.

### HALT Is Not Optional

The original Chip-8 has no HALT instruction. Programs are expected to loop forever or crash. For testing, this is a problem — a VM with no clean termination walks through zero-filled memory until it hits the boundary. Adding `0x0000` as HALT (our extension) made every test terminate cleanly. Lesson: a system without a clean exit is a system you can't test properly.

### Tests Are Code, and Code Has Bugs

Three out of five "bugs" were in the test suite, not the implementation. The `and_or_xor` encoding error was particularly insidious — the VM did exactly what the bytes said, which was different from what the comments said. The test passed for the wrong reasons on some paths (V2 happened to equal 0xFF via OR instead of LD). 

This is the same lesson as the allocator: verify what you think you're testing, not what you hope you're testing.

### VF Is a Trap

Register 0xF is used as a flag register (carry, borrow, shift-out) by arithmetic instructions. Any value stored there gets clobbered by the next arithmetic operation. This is documented in the Chip-8 spec but easy to forget. The `vf_clobber` stress test catches this class of bug.

### Bytecode Encoding Is a Serialization Problem

The `8xy*` instructions encode the operation type in the *lowest nibble* of the second byte, not the second nibble overall. So `LD V2, V0` is `8200` (byte pair `0x82, 0x00`), not `8201`. The `and_or_xor` bug happened because I wrote the byte pairs intuitively rather than mechanically. Lesson: when encoding bytecode by hand, always write the opcode first and derive the bytes, never the other way around.

## Architecture Decisions

- **Big-endian opcodes** — matches Chip-8 convention, makes `fetch16` simple
- **4KB memory** — standard Chip-8, programs start at 0x200
- **16-entry call stack** — standard Chip-8, separate from data memory
- **0x0000 = HALT** — our extension, essential for testability
- **Error codes over panics** — the VM returns error codes, never crashes. Callers decide what to do.

## What's Next

Stage 2: Custom ISA (Lantern Bytecode) — variable-length instructions, typed values, stack frames, local variables. The Chip-8 is a learning exercise; the Lantern bytecode is the real VM.

## Bug Trajectory Update

| Stage | Bugs | Where |
|-------|------|-------|
| Allocator Stage 1 | 5 | Implementation |
| Allocator Stage 3 | 0 | Implementation |
| Allocator Stage 5 | 1 | Concurrency surfaced ownership bug |
| VM Stage 1 | 5 | **All in tests** |

The pattern holds: early stages have bugs, verification catches them. New observation: **tests are code, and code has bugs.** The implementation was correct; the tests were wrong. Five test bugs, zero implementation bugs. The verification lesson works — but you have to verify the verification too.