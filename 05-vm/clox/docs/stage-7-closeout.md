# Stage 7 Close-out — clox stdlib

**Author:** Mira
**Date:** 2026-07-10
**Branch:** `stage-07-stdlib`
**Commit:** `6f16847`

## What this stage is

The next move after Stage 6 (REPL + stack traces). I picked **stdlib** as
the smallest, most reversible addition that taught the right lessons.

The lessons I wanted:
- **The VM ↔ C boundary.** Each native function is a C function pointer
  stored in `vm.globals` as an `ObjNative`. Calling one is `OP_CALL` with
  the native as the callee. The native gets `argCount` and a pointer to
  the args on the stack.
- **The GC and native-returned values.** When a native returns a heap
  object (like a string), the GC must be able to find it. `copyString()`
  and `takeString()` register the new object with the GC. Allocating a
  temp buffer with `ALLOCATE` and not freeing it is a leak; valgrind
  catches this.
- **The value type system.** `typeof()` is a function over every value
  type in clox. Adding a new value type requires extending the switch.
  This is the cost of a runtime type predicate written in C instead of
  as a VM opcode.
- **The "namespace" question.** Lox has no static methods, so a class-
  as-namespace pattern would require either a new opcode or a hack.
  Flat namespace with prefixed names (`number_abs`, `string_length`)
  is the convention I picked.

## What I built

Seven new native functions in `src/native.c`, registered as flat globals
in `defineNatives()`:

| Function | Args | Returns | What it does |
|----------|-----:|--------:|--------------|
| `number_abs(x)` | 1 | number | `\|x\|` |
| `number_min(a, b)` | 2 | number | smaller of two |
| `number_max(a, b)` | 2 | number | larger of two |
| `string_length(s)` | 1 | number | byte count of string |
| `string_upper(s)` | 1 | string | uppercase copy |
| `string_lower(s)` | 1 | string | lowercase copy |
| `typeof(v)` | 1 | string | type name as string |

Each follows the existing `clock()` native's protocol:
- Signature `(int argCount, Value *args)` where `args` points to the
  first argument on the VM stack.
- Returns a `Value`; the VM's `callValue()` pops the args and pushes
  the result.
- `runtimeError()` on wrong arity or wrong type. The longjmp means
  the return value after `runtimeError()` is unreachable, but the C
  compiler doesn't know that, so I return `NIL_VAL` as a placeholder.

## What I deliberately did NOT do

- **Add arrays.** clox has no array type (`OBJ_ARRAY` doesn't exist in
  `object.h`). Without arrays, `string_split` is not implementable
  (no return type). Defer until I add arrays in a future stage.
- **Add class-as-namespace (`number.abs(x)` syntax).** This would
  require either a new opcode (`OP_GET_STATIC_PROPERTY`) or extending
  `OP_GET_PROPERTY` to work on classes. Both are language-semantics
  changes I didn't want to bundle with a stdlib PR. The flat namespace
  is reversible: a future stage can add static methods and re-home the
  natives without breaking the existing call sites.
- **Add `string_substring`, `string_contains`, `string_replace`.** All
  of these can return strings, so they're in the same family. Defer
  to Stage 8 (or "stage N+1" — I am intentionally not committing to a
  stage number, because per Tom's instruction to "iterate," a
  "stdlib" arc is more honest as multiple small stages than one
  big one).
- **Touch `vm.c`, `compiler.c`, `gc.c`, `main.c`.** The whole stdlib
  is a single-file change in `native.c`. The VM, the compiler, the
  REPL, and the GC are all unchanged. This is the reversibility
  signal: if I want to roll this back, the diff is `git revert
  6f16847`.

## Verification

```
$ cd 05-vm/clox && make clean && make
[no warnings under -Wall -Wextra -std=c99 -pedantic]

$ make test
==> bin/test_clox
15 passed, 0 failed
==> bin/test_repl
5 passed, 0 failed
==> bin/test_stdlib
7 passed, 0 failed

$ make valgrind
==> valgrind bin/test_clox
[in use at exit: 0 bytes in 0 blocks, 0 errors]
==> valgrind bin/test_repl
[in use at exit: 0 bytes in 0 blocks, 0 errors]
==> valgrind bin/test_stdlib
[in use at exit: 0 bytes in 0 blocks, 0 errors]
```

GC stress test (10,000 iterations, allocating strings via
`string_upper`):

```
$ valgrind ./bin/clox /tmp/clox_gc_stress.lox
in use at exit: 0 bytes in 0 blocks
total heap usage: 20,058 allocs, 20,058 frees, 352,850 bytes allocated
ERROR SUMMARY: 0 errors from 0 contexts
```

20,058 allocs / 20,058 frees — every string allocated by
`string_upper` is matched by a free, and the GC sees every
intermediate.

Manual smoke:

```
$ printf 'var s = "Hello, World!";\nprint string_upper(s);\nprint string_length(s);\nprint typeof(s);\n' | ./bin/clox --repl
> > HELLO, WORLD!
> 13
> string
>
```

Arity error path:

```
$ ./bin/clox foo.lox
Error: number_abs() takes 1 argument (0 given).
[line 28] in script
$ echo $?
70
```

## Bug caught during implementation

`copyString("string_length", 12)` was off by one — actual length is
13. The `string_length` and `string_upper`/`string_lower` natives
silently failed to register (the symbol was interned with the wrong
length, so the tableSet stored it, but later lookups with the correct
length didn't find it). Tests caught this immediately because
`string_length` and `string_upper` failed with "Undefined variable"
while `number_abs` and `typeof` (which I had correct-lengthed) worked.

**Fix:** replaced hardcoded lengths with `strlen()` in all native
registrations, so future additions can't have the same class of bug.
**Lesson:** the test caught the bug because the test asserts on
*which* functions work, not just *that some* functions work. A test
that asserted "stdlib has at least N natives" would have passed with
the bug present. The granularity of the assertions matters.

## Why stdlib and not modules (the other candidate)

I had been leaning toward modules as Stage 7 in my pre-Stage-6 analysis.
The retrospective listed modules as the highest-leverage feature
("the one I notice missing every time I try to use the REPL for more
than a 30-line demo"). I switched to stdlib for this stage because:

- **Modules is a 600-line change** with multiple design decisions
  (file paths relative to what? recursive imports? circular detection?
  `from X import Y` vs `import X`?). Any one of those decisions made
  wrong would require a rework of the whole module.
- **Stdlib is a 100-line change** with one design decision (namespace
  convention, which I picked) and no new language semantics.
- **The lesson-to-line ratio is higher for stdlib.** Native functions
  teach the VM↔C boundary, the GC's view of heap objects, and the
  value type system. Modules would teach me mostly about file I/O
  and namespace mechanics, which I already know from other projects.
- **Reversibility.** Modules is harder to back out of — a half-baked
  module system is a real cost. A half-baked stdlib is one
  `git revert 6f16847` away.

Modules is still in the running for Stage 8 or later, when I have a
few more stdlib pieces to motivate the call patterns.

## The pattern this stage reinforces

Tom said: "Iterate and build. The build-your-own-x project is
educating you." Stage 7 is the first stage I picked *myself* (with
Tom's delegation), and the picking taught me something: **a small
stage that ships today is worth more than a big stage that might
ship next week.** I could have started modules. I would have been
in the middle of a 600-line design decision three days from now. By
picking the small stage, I shipped in one sitting, learned the
VM↔C boundary lesson, and got to *write this close-out doc* — which
is the educational artifact Tom was asking for. The next stage will
be smaller too, because that's the rhythm.
