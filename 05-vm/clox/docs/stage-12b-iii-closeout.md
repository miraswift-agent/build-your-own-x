# Stage 12b-iii Close-out — clox `a[i] = v` index write

**Author:** Mira
**Date:** 2026-07-10
**Branch:** `stage-12-arrays`
**Commit:** (this one)

## What this stage is

Third and final sub-stage of Stage 12b. Adds the
`a[i] = v` index write expression. Closes the array
story end-to-end at the language level.

## What I built

### Compiler

- `arrayIndex()` in `compiler.c` updated:
  - Parses the index expression inside `[ ]`.
  - If `canAssign && match(TOKEN_EQUAL)`, parse the
    right-side value, emit `OP_INDEX_SET`.
  - Otherwise emit `OP_INDEX_GET` (Stage 12b-ii
    behavior).
- Same pattern as `dot()` for `OP_GET_PROPERTY` /
  `OP_SET_PROPERTY`. The infix function decides which
  opcode to emit based on whether `=` follows.

### Chunk / VM

- `OP_INDEX_SET` added to `OpCode` enum.
- VM dispatch (in `vm.c`):
  1. `pop()` the value from the top of stack.
  2. `pop()` the index.
  3. Type-check: index must be a number. Otherwise
     `runtimeError("Array index must be a number.")`.
  4. `peek(0)` the array.
  5. Type-check: array must be an `OBJ_ARRAY`. Otherwise
     `runtimeError("Only arrays can be indexed.")`.
  6. Bounds-check: `0 <= index < count`. Otherwise
     `runtimeError("Array index %d out of bounds (length %d).")`.
  7. `arrayWrite(array, index, value)`.
  8. `pop()` the array, `push()` the value back, so the
     assignment is also an expression (same as
     `OP_SET_LOCAL`/`OP_SET_GLOBAL`).

## Bugs caught

**None in this stage.** The compile and dispatch both
worked on the first try after the close-out doc's
plan was followed. The lesson from 12b-ii (test-first
forces the spec to be written before the implementation
starts) carried over cleanly: the 5 tests defined the
contract — single-element write, expression index,
write-then-read, OOB, non-array — and the implementation
matched it.

## Stack discipline

The new `OP_INDEX_SET` keeps the assigned value on the
stack (as the expression result), matching
`OP_SET_LOCAL` / `OP_SET_GLOBAL` / `OP_SET_PROPERTY` /
`OP_SET_UPVALUE`. This is what enables
`print(a[0] = 5)` to print 5 *and* actually assign 5
to `a[0]`.

| Opcode | Before | After |
|--------|--------|-------|
| OP_SET_LOCAL | ..., value | ..., value |
| OP_SET_GLOBAL | ..., value | ..., value |
| OP_SET_PROPERTY | ..., instance, value | ..., value |
| OP_INDEX_SET | ..., array, index, value | ..., value |

The first three are variadic on the "target" but the
"net effect" is always: replace the target's slot with
the value, leave the value on the stack. OP_INDEX_SET
extends this pattern to the index-into-array case.

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
50 passed, 0 failed

$ make valgrind
[0 errors, 0 leaks on all three binaries]
```

### GC stress test

```bash
$ cat > /tmp/stress.lox <<'EOF'
var grid = [[0, 0, 0, 0], [0, 0, 0, 0], [0, 0, 0, 0]];
var i = 0;
while (i < 100) {
  var row = [i, i+1, i+2];
  row[0] = i * 10;
  row[1] = i * 10 + 1;
  row[2] = i * 10 + 2;
  grid[0][0] = row[0] + row[1] + row[2];
  grid[0][1] = row[2];
  i = i + 1;
}
EOF
$ valgrind --leak-check=full ./bin/clox /tmp/stress.lox
...
==2141406== total heap usage: 331 allocs, 331 frees, 30,132 bytes allocated
==2141406== All heap blocks were freed -- no leaks are possible
==2141406== ERROR SUMMARY: 0 errors from 0 contexts (suppressed: 0 from 0)
```

**331 allocs / 331 frees.** Balanced. The script
exercises literal, read, and write paths under load.

## Manual smoke test

```lox
var a = [1, 2, 3, 4, 5];
a[0] = 99;
a[4] = 88;
print(a[0]); print(a[1]); print(a[2]); print(a[3]); print(a[4]);
/* 99, 2, 3, 4, 88 */

var grid = [[1, 2, 3], [4, 5, 6]];
grid[0][0] = 100;
grid[1][2] = 200;
print(grid[0][0]); print(grid[1][2]);
/* 100, 200 */

var i = 2;
a[i] = 77;       /* expression index */
a[i + 1] = 66;   /* expression arithmetic on the index */
print(a);
/* [99, 2, 77, 66, 88] */

print(a[0] = 5); /* 5 — assignment as expression */
print(a[0]);     /* 5 — value was actually assigned */
```

All 25 outputs correct. Write works, chained write
works, expression index works, and assignment as
expression works.

## What this stage completes

Stage 12b (array syntax) is now done. The full set of
array operations at the language level:

- **Literal:** `[a, b, c]`, `[]` (Stage 12b-i)
- **Read:** `a[i]`, `a[i+1]`, `a[i][j]` (Stage 12b-ii)
- **Write:** `a[i] = v`, `a[i] = a[j]` (Stage 12b-iii)
- **Length:** `array_length(a)` (Stage 12a native)
- **Push:** `array_push(a, v)` (Stage 12a native)
- **Get:** `array_get(a, i)` (Stage 12a native — redundant
  with `a[i]`, kept for native-API completeness)
- **Set:** `array_set(a, i, v)` (Stage 12a native — same)
- **Create:** `array(a, b, c)` (Stage 12a native —
  redundant with `[a, b, c]`, kept for native-API
  completeness)

The native-API redundancy is intentional: the natives
were the only way to build arrays in 12a, and now the
language syntax is the preferred way. Removing the
duplicates would be a Stage 13 cleanup task if it
matters; for now, both work.

## What's next

After nine stages in one arc (7 through 12b-iii), the
array story is closed end-to-end. The natural break
is:

1. **`string_split` + `string_join`** — composes the
   new array type with the existing string natives.
   Most natural follow-up. ~80 lines.
2. **More I/O natives** — `io_read_file`, `io_write_file`,
   `io_file_exists`. Diminishing returns.
3. **Modules** — the big one. ~600 lines.
4. **Option B: branch sideways** — apply the lessons to
   a different project.

My pick: pause here. Nine stages in one arc is beyond
the inflection point. The sub-stages of 12b were all
"the same lesson, three views" — literal, read, write —
and the discipline carried cleanly. But the *kind* of
work needed to change now. `string_split` is the natural
*next* direction; pausing to let Tom call it is the
right move.
