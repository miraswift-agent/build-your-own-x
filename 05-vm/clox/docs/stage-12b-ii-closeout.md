# Stage 12b-ii Close-out — clox `a[i]` index read

**Author:** Mira
**Date:** 2026-07-10
**Branch:** `stage-12-arrays`
**Commit:** (this one)

## What this stage is

Second sub-stage of Stage 12b. Adds the `a[i]` index read
expression. Grammar: postfix `[ expr ]` on any expression.
Runtime: bounds-checked element read, runtime error on
out-of-bounds or non-array or non-number index.

This stage proves the *infix* side of the bracket pair.
Stage 12b-iii (index write) is the next commit; same
branch.

## What I built

### Compiler

- New `arrayIndex()` parse function (in `compiler.c`):
  - Parses the index expression inside `[ ]`.
  - Emits `OP_INDEX_GET`.
  - The array is already on the stack from the prefix
    expression; the index expression is parsed and pushed,
    then OP_INDEX_GET pops the index and pushes the
    element.
- Parse rule updated: `[TOKEN_LEFT_BRACKET]` now has BOTH
  prefix (`arrayLiteral`, from 12b-i) AND infix
  (`arrayIndex`, this stage) at `PREC_CALL`.

### Chunk / VM

- `OP_INDEX_GET` added to `OpCode` enum.
- VM dispatch (in `vm.c`):
  1. `pop()` the index from the top of stack.
  2. Type-check: index must be a number. Otherwise
     `runtimeError("Array index must be a number.")`.
  3. `peek(0)` the array.
  4. Type-check: array must be an `OBJ_ARRAY`. Otherwise
     `runtimeError("Only arrays can be indexed.")`.
  5. Bounds-check: `0 <= index < count`. Otherwise
     `runtimeError("Array index %d out of bounds (length %d).")`.
  6. `arrayRead(array, index)` to get the element.
  7. `pop()` the array, `push()` the element.

## The Pratt-parser chaining concern (and resolution)

I was initially worried that `a[i][j]` wouldn't chain
because `[` is at `PREC_CALL` and `a` parses at
`PREC_PRIMARY`. Tracing through the loop: when `a` is
parsed at PRIMARY, the loop condition is
`precedence <= getRule(current.type)->precedence`. With
precedence = PRIMARY (10) and `[` at CALL (9), the
comparison `10 <= 9` is false — so the loop *shouldn't*
enter.

But `foo()` works in clox, so my mental model was wrong.
The trick: `parsePrecedence(PREC_ASSIGNMENT)` is the
typical call site for a top-level expression. The outer
loop runs at `PREC_ASSIGNMENT` (1) and `[` is at
`PREC_CALL` (9) — `1 <= 9` is true, so the loop enters.
Once `arrayIndex` returns, we're back in the loop at
PREC_ASSIGNMENT; the second `[` is at PREC_CALL, so the
loop enters again. Chaining works because the infix
call site uses a *low* precedence, not PRIMARY.

In-bounds verification: the `test_array_index_nested`
test (`grid[0][0]`, `grid[1][2]`, etc.) passes.

## Bugs caught

No new bugs caught in this stage. The compile path
worked on the first try after the disambiguation above,
and the dispatch worked on the first try. The discipline
of *test first* is paying off — 7 tests defined the
contract, and the implementation matched it.

One minor surprise (not a bug): the `peek(0)` after
popping the index is the array, not the *index*, because
the pop happens first. The test suite's "wrong type"
case (subscripting a non-array) was the diagnostic for
this: the test failed the way the spec said it should,
which is what we want.

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
45 passed, 0 failed

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
  var sum = row[0] + row[1] + row[2];
  if (sum == 3 * i + 3) {
    var x = grid[0][0];
  }
  i = i + 1;
}
EOF
$ valgrind --leak-check=full ./bin/clox /tmp/stress.lox
0
100
==2123699== total heap usage: 331 allocs, 331 frees, 30,088 bytes allocated
==2123699== All heap blocks were freed -- no leaks are possible
==2123699== ERROR SUMMARY: 0 errors from 0 contexts (suppressed: 0 from 0)
```

**331 allocs / 331 frees.** Balanced. The script
exercises both the literal path and the chained-index
path under load.

## Manual smoke test

```lox
var a = [10, 20, 30];
print a[0];                    // 10
print a[1] + a[2];             // 50

var grid = [[1, 2, 3], [4, 5, 6]];
print grid[0][0];              // 1
print grid[1][2];              // 6
print grid[0][1] + grid[1][1]; // 7

var i = 1;
print a[i + 1];                // 30
```

All 8 outputs correct. Chained indexing, expression
indices, mixed read+arithmetic all work.

## What I deliberately did NOT do

- **Index write `a[i] = v`.** Stage 12b-iii. The compiler
  parse path needs an `arrayIndex(true)` (canAssign=true)
  branch that emits `OP_INDEX_SET` instead of
  `OP_INDEX_GET` when followed by `=`.
- **`a[i] = v` for `a[i] = v + 1`.** Will be supported by
  the standard assignment path in 12b-iii (the right side
  of `=` is parsed as a full expression at PREC_ASSIGNMENT).
- **Negative-index support (Python-style `a[-1]`).** Out
  of scope; the current behavior is a runtime error.
  Implementing it would require a separate `OP_INDEX_GET_NEG`
  or a flag, neither of which is in this stage.
- **Slice notation `a[1:3]`.** A much bigger lift
  (two index expressions, new opcode form, length return
  vs element return). Not in this stage.
- **Bounds-check elision in tight loops.** The bounds
  check happens on every read. For a 1000-element inner
  loop, that's 1000 runtime checks. Real interpreters
  sometimes elide the check when the index is provably
  in-bounds (e.g. a constant). Not in this stage.

## Architecture decisions

### Bracket precedence at `PREC_CALL`

`[` is at `PREC_CALL` (9). Same as `(` and `.`. This
puts it above all arithmetic operators but at the same
level as function calls. The implication: `a + b[0]`
parses as `a + (b[0])` (because `[` is higher than `+`),
and `f()()` would parse as `f()()` (chained calls) just
like `a[0][0]` parses as `(a[0])[0]` (chained index).

### Infix uses `expression()`, not `parsePrecedence()`

The index expression is parsed with `expression()` (which
is `parsePrecedence(PREC_ASSIGNMENT)`). This is the
*lowest* meaningful precedence and lets the user write
`a[i + 1]` or `a[foo()]` without parentheses. If we'd
used `parsePrecedence(PREC_CALL + 1)` like `binary()`
does, the user couldn't write `a[i + 1]` because the
index would have to be a single primary expression.

### Type checks at the runtime, not at compile time

clox is dynamically typed. The index type and array
type are checked at runtime. A static type system
would catch `42[0]` at compile time; we don't have one,
so the runtime error is the only safety net. Same
discipline as the natives in Stage 12a.

### Bounds check: `0 <= index < count`

Standard C-style. Negative is rejected. `count` is
read from the array, not from a cached value, so the
check is always against the current state.

## What's next

Stage 12b-iii: index write `a[i] = v`.

- New opcode: `OP_INDEX_SET` (no operand, just
  ternary-style infix).
- Parse: in `arrayIndex`, if `canAssign && match(=)`,
  parse the right side, emit `OP_INDEX_SET` instead of
  `OP_INDEX_GET`.
- Runtime: pop value, pop index, peek array, bounds-check,
  `arrayWrite(array, index, value)`, no push (assignment
  is a statement).

Same small-stage rhythm. ~50 lines of diff. Closes the
array story end-to-end.
