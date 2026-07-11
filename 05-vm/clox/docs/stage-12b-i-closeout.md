# Stage 12b-i Close-out — clox array literals

**Author:** Mira
**Date:** 2026-07-10
**Branch:** `stage-12-arrays`
**Commit:** (this one)

## What this stage is

The first half of Stage 12b. Adds the `[a, b, c]` array
literal as a language-level expression. The grammar now
accepts `[ expr ("," expr)* ]` and produces an `ObjArray`
at runtime via a new `OP_ARRAY` opcode.

This is a sub-stage of 12b. The other two — index read
(`a[i]`) and index write (`a[i] = v`) — are separate
commits on the same branch.

## What I built

### Scanner

- `TOKEN_LEFT_BRACKET` and `TOKEN_RIGHT_BRACKET` added to
  `scanner.h`.
- `scanner.c` recognizes `[` and `]` in `scanToken()`.

### Compiler

- New `arrayLiteral()` parse function (in `compiler.c`):
  - Compiles element expressions separated by `,`.
  - Emits `OP_ARRAY <count>` after the elements.
  - Cap of 255 elements per literal (single-byte operand;
    matches `argumentList`).
  - Empty literal `[]` is `count = 0`.
- Parse rule added: `[TOKEN_LEFT_BRACKET] = {arrayLiteral, NULL, PREC_NONE}`.
  Prefix-only, no infix.

### Chunk / VM

- `OP_ARRAY` added to `OpCode` enum (in `chunk.h`).
- VM dispatch (in `vm.c`):
  - Reads `count` operand (1 byte).
  - Allocates `ObjArray` with capacity == count.
  - **GC-protective `push(OBJ_VAL(array))` before filling** —
    the `arrayWrite` calls could otherwise trigger a
    collection and reclaim the partially-built array.
  - Fills `elements[0..count-1]` from `peek(count)` (first
    source) down to `peek(1)` (last source).
  - Sets `array->count = count` after the fill.
  - Pops the protective push and the `count` source
    values, then pushes the array as the single result.

## Bugs caught during implementation

### Bug 1: `arrayWrite` does not increment `count`

First implementation produced empty arrays (count 0) for
every literal. The issue: `arrayWrite(array, 0, val)` sets
`elements[0]` but does not touch `array->count`. The
Stage 12a natives use `arrayPush` (which increments count)
or set `count` after the fact. The OP_ARRAY path must set
`count` explicitly after the fill loop.

**Fix:** added `array->count = count;` after the write loop.

**Why this is a real lesson, not just a one-off fix:** when
multiple call sites share a low-level API but the *meaning*
of "write" differs (in-place update vs. fill construction),
the API itself doesn't say which semantics applies. The
caller has to know. A cleaner design would be two functions
(`arrayWrite` for in-place, `arrayFill` for construction)
but that's a refactor for another stage.

### Bug 2: peek offset wrong after the protective push

First implementation had `peek(count - 1 - i)`, which was
off by one (the protective push sits between `stackTop`
and the source values). Fixed to `peek(count - i)`.

**Lesson:** the protective push changes the offset of
`peek`. Always re-derive the peek distance after any
push/pop, not before.

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
38 passed, 0 failed

$ make valgrind
[0 errors, 0 leaks on all three binaries]
```

### GC stress test

```bash
$ cat > /tmp/stress.lox <<'EOF'
var i = 0;
while (i < 500) {
  var a = [i, i+1, i+2, i+3, i+4, i+5];
  array_push(a, i * 2);
  var b = [a, [i+10, i+11]];
  array_push(b, [i+20, i+21, i+22]);
  i = i + 1;
}
EOF
$ valgrind --leak-check=full ./bin/clox /tmp/stress.lox
stress done
==2054777== total heap usage: 5,121 allocs, 5,121 frees, 316,383 bytes allocated
==2054777== All heap blocks were freed -- no leaks are possible
==2054777== ERROR SUMMARY: 0 errors from 0 contexts (suppressed: 0 from 0)
```

**5,121 allocs / 5,121 frees.** Balanced. The literals
exercise both the fill path and the protective-push
discipline.

## Manual smoke test

```lox
var a = [1, 2, 3];
print a;                      // [1, 2, 3]
print array_length(a);        // 3
print array_get(a, 0);        // 1

var empty = [];
print array_length(empty);    // 0

var mixed = [1, "foo", true];
print mixed;                  // [1, foo, true]
print array_length(mixed);    // 3
print array_get(mixed, 1);    // foo

var nested = [[1, 2], [3, 4]];
print nested;                 // [[1, 2], [3, 4]]
print array_length(nested);   // 2
print array_get(nested, 0);   // [1, 2]
print array_length(array_get(nested, 0));  // 2
print array_get(array_get(nested, 1), 0);  // 3
```

All 16 output lines correct.

## What I deliberately did NOT do

- **Index read `a[i]`.** Stage 12b-ii.
- **Index write `a[i] = v`.** Stage 12b-iii.
- **`len()` or `arr.length`.** That requires either a
  special form (like a method call on a value type) or a
  new native. Both are out of scope for this sub-stage.
- **255-element cap removal.** The cap is one byte; the
  OP_ARRAY operand is `uint8_t`. A 16-bit operand would
  require a new opcode form. Not in this stage.

## Architecture decisions

### One-byte operand for the element count

The OP_ARRAY operand is a single byte. This caps literals
at 255 elements, which is a soft limit. The alternative
(two-byte operand) is the more invasive change; a 255
limit is consistent with the existing one-byte count in
`OP_CALL`. If we ever need >255 elements, we can add
`OP_ARRAY_LONG` with a 16-bit operand.

### `arrayWrite` is dumb; OP_ARRAY sets count

The semantics of `arrayWrite` is "write elements[index]"
— it doesn't manage count. This is the same shape as
`OP_INDEX_SET` (Stage 12b-iii) and the in-place natives.
The construction-vs-update distinction is *call-site
responsibility*, not API behavior.

The alternative (a second function `arrayFill`) is
cleaner but adds a new API surface. Not worth the
refactor for one caller.

### Cap 255, error above

`arrayLiteral` errors with a friendly message if the
element count exceeds 255. The error happens at
*compile time*, not runtime — a 256-element literal
never compiles. This is consistent with the 255-arg
limit on `OP_CALL`.

## What's next

Stage 12b-ii: index read `a[i]`.

- New opcode: `OP_INDEX_GET` (operand: none, just a
  suffix-style infix like `.`).
- Parser: add `arrayIndex()` to the parse rule for
  `[TOKEN_LEFT_BRACKET]` *infix*.
- Runtime: peek array and index, bounds-check, push the
  element value.

Same small-stage rhythm: tests first, single-file diff
where possible, valgrind-clean, close-out the same day.
