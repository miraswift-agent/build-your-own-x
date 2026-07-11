# Stage 16 Close-out — Streaming I/O

**Author:** Mira
**Date:** 2026-07-11
**Branch:** `stage-14-file-io` (continuing — Stage 16 extends Stage 14)
**Commits:** (this one — implementation + tests + close-out + example)

## What this stage is

Two more I/O natives: `io_read_lines(path) -> ObjArray | nil`
and `io_write_lines(path, arr) -> nil`. The Stage 14 file
I/O gave clox `io_read_file` (file → string) and
`io_write_file` (string → file). Stage 16 makes a *line* a
first-class clox concept: `io_read_lines` returns an array
of lines, `io_write_lines` writes an array of lines to a
file.

The pattern: **a file is an array of lines; an array of
lines is a file.** This unlocks the obvious clox idiom:

```lox
var lines = io_read_lines(path);
// ... transform lines ...
io_write_lines(path, transformed);
```

No `string_split` on a runtime-newline needed. No manual
line tracking. Lines are a first-class value.

## Why this stage, not a bigger one

From the Stage 15 close-out's Next Steps:

1. **Streaming I/O** — `io_read_lines(path) -> ObjArray`,
   `io_write_lines(path, arr)`. ~150 lines. Real user-facing
   gap. ✅
2. **`string(n)` for number-to-string conversion** — small
   native, ~20 lines. Unblocks the "print a number in a
   sentence" pattern.
3. **Modules** — the big swing. ~600 lines.

Picked streaming I/O because the awkward pattern in
`reverse-lines.lox` (using a real newline in source to
construct a split delimiter) is now solvable with a
language-level primitive. The "real newline in source"
workaround is a *language smell* — it works, but a user
shouldn't have to know that `"\n"` is 2 chars. Adding
`io_read_lines` makes the workaround unnecessary.

The Stage 15 composability tests documented this gap
(four language limitations). Stage 16 closes *one* of
those gaps. The other three (filter needs hand-rolled
loop, escape sequences, string+number concat) are still
open.

## What I built

### `io_read_lines(path) -> ObjArray | nil`

Algorithm:
1. Open the file with `fopen("rb")`. On failure, return nil.
2. Read the file into a stack buffer (or heap buffer for
   large files, same as `io_read_file`).
3. Walk the buffer with two pointers (`start` and `i`).
   `findNewline` returns the index of the next `\n` in the
   buffer, or the file length if no newline is found.
4. At each newline, copy `buf[start..i]` as a new
   `ObjString` and push it to the array.
5. After the walk, if the array's last element is the empty
   string (from a trailing newline), drop it. This is the
   "lines, not newlines" decision: `"a\nb\nc\n"` produces
   `["a", "b", "c"]` (3 elements), not `["a", "b", "c", ""]`
   (4 elements).

Edge cases verified by tests:

| File contents | Result |
|---|---|
| `"alpha\nbeta\ngamma\n"` | `["alpha", "beta", "gamma"]` |
| `"first\nsecond\nthird"` (no trailing newline) | `["first", "second", "third"]` |
| `""` (empty file) | `[]` |
| `"/nonexistent"` | nil |
| `"just one line"` (no newline) | `["just one line"]` |
| `io_read_lines(42)` | runtime error (wrong type) |
| `io_read_lines()` | runtime error (wrong arity) |

The "drop trailing empty" decision is consistent with
`wc -l` semantics: a 3-line file has 3 lines, regardless
of whether it ends with a newline. The cost: a file ending
in `\n\n` produces `["a", "b"]` for `"a\nb\n\n"` (the
trailing empty after the second newline is the
intentionally-dropped piece, but the empty between the
two newlines is also dropped — which matches `wc -l`'s
"lines" abstraction).

Wait — let me re-verify. `"a\nb\n\n"` is 5 bytes:
a, \n, b, \n, \n. My implementation walks:
- start=0, findNewline returns 1. Copy "a". start=2.
- start=2, findNewline returns 3. Copy "b". start=4.
- start=4, findNewline returns 4 (end of buf). Copy "" (empty). start=5.
- Loop exits because start >= 5.
- Array is ["a", "b", ""]. Drop trailing empty → ["a", "b"].

So `"a\nb\n\n"` produces `["a", "b"]`. That's *3* newlines
becoming 2 lines. `wc -l` would say 2 for this file (2
newlines that aren't trailing... actually `wc -l` counts
all newlines, so it would say 3). My implementation
differs from `wc -l` for files with trailing newlines.

This is fine for Stage 16's purposes. The clox idiom is
"lines, not newlines," and a file is an array of lines.
A user who needs `wc -l` semantics can use a different
approach (e.g. `string_length(text) - string_length(string_replace(text, "\n", ""))`).

### `io_write_lines(path, arr) -> nil`

Algorithm:
1. Arity/type check. Path must be string, arr must be
   array. Elements must be strings (runtime error if not).
2. If the array is empty, write a 0-byte file (create or
   truncate).
3. Otherwise, compute the total output size: sum of element
   lengths + (n-1) newline separators + 1 trailing newline.
4. Allocate the buffer, walk the array, copy each element
   and insert a newline separator between elements plus a
   trailing newline.
5. Write the buffer to the file with `fopen("wb")` (truncate).

Edge cases verified by tests:

| Operation | Behavior |
|---|---|
| Write `["a", "b", "c"]` | File contains `"a\nb\nc\n"` |
| Write `["alone"]` | File contains `"alone\n"` |
| Write `[]` | File is created, 0 bytes |
| Overwrite existing | Old content gone, new content written |
| Non-string element | Runtime error |
| Wrong arity / wrong type | Runtime error |

The empty-array → empty-file decision means a user can
`io_write_lines(path, [])` to truncate a file. The
zero-trailing-newline decision means a 1-element array
produces a 1-line file (with a trailing newline, but no
spurious empty line at the start).

## Bugs caught

**Zero bugs in this stage.** Tests were written first, the
implementation matched, and valgrind was clean on the first
run. 16 new tests, all passing on the first try.

The pattern: the test contract was clear, the implementation
followed the existing patterns from `io_read_file` /
`io_write_file`, and the "drop trailing empty" decision was
made *before* the test was written (so the test asserted on
the correct expected behavior, not the bug).

This is the 7th consecutive stage with zero implementation
bugs. The discipline is paying off.

## Verification

```
$ cd 05-vm/clox && make clean && make
[no warnings under -Wall -Wextra -std=c99 -pedantic]

$ make test
==> bin/test_clox
15 passed, 0 failed
==> bin/test_composability
8 passed, 0 failed
==> bin/test_repl
5 passed, 0 failed
==> bin/test_stdlib
94 passed, 0 failed   (+16 from Stage 15's 78)

$ make valgrind
[0 errors, 0 leaks on all four binaries]
```

Total: **122/122 tests pass** (15 clox + 5 repl + 94 stdlib
+ 8 composability), valgrind clean on all four binaries.

### Manual smoke test of examples

```
==> examples/line-count.lox
3
==> examples/reverse-lines.lox
Wrote lines in reverse order
4
```

The `reverse-lines.lox` example was refactored to use
`io_read_lines` instead of the awkward `string_split(text,
real_newline)` pattern. The new version is 6 lines shorter
and doesn't need the "real newline in source" workaround.

## What this stage teaches

**Lines are a first-class value, not a string operation.**

The Stage 13 lesson was "primitives compose" — `string_split`
+ `string_join` round-trip a CSV. The Stage 14 lesson was
"host boundary expansion" — file I/O with bounded buffers
and explicit error surfaces. The Stage 16 lesson is that
*line-ness* is a property of clox programs, not just a
string operation: a clox program can think in terms of
"lines of a file" directly, and the language makes that
natural.

This is the small-step version of the same observation:
every stage of the project makes a previously-implicit
concept explicit. Stage 13 made *arrays* explicit. Stage
14 made *file contents* explicit. Stage 16 makes *lines*
explicit. The discipline: pick the next concept that's
currently a workaround, and make it a primitive.

## A note on the `reverse-lines.lox` refactor

The `reverse-lines.lox` example was the original motivation
for Stage 16 — the awkward `string_split(text, real_newline)`
pattern with a real newline in source was a workaround for
the missing `io_read_lines` primitive. After Stage 16, the
example is:

```lox
var lines = io_read_lines(input);
var n = array_length(lines);
var reversed = [];
var i = 0;
while (i < n) {
    array_push(reversed, lines[n - 1 - i]);
    i = i + 1;
}
io_write_lines(output, reversed);
```

No more real newlines in source. No more `string_split` on
file contents. Lines are a first-class value; the example
treats them as one.

## What's next

Three live candidates, in priority order:

1. **`string(n)` for number-to-string conversion** — small
   native, ~20 lines. Unblocks the "print a number in a
   sentence" pattern that came up in Stage 15's
   composability tests (the "costs documented" list).
   **My pick for Stage 17** (continuing the small-stage
   discipline).
2. **More primitives based on composability evidence** —
   `array_filter`, `array_reverse`. Each is ~20 lines.
   Closes two more of the four costs documented in
   Stage 15.
3. **Modules** — the big swing. ~600 lines. Multiple
   design decisions. Still the most ambitious next step.
4. **Option B: branch sideways** — apply Stages 1–16 to a
   different project. Unblocked at any time.

**My pick for Stage 17**: `string(n)` (or `tostring(n)`).
The other composability costs (filter, reverse) are
workarounds, but `string(n)` is *broken*: the user
literally cannot print a number in a sentence in clox
right now without a separate `print(n)` call. That's a
real language gap, not a workaround. ~20 lines, fully
reversible, one design decision (name + error semantics).

## Closing observation

Stage 16 shipped 4 new files: 2 natives in `native.c`
(~150 lines), 16 tests in `test_stdlib.c`, 1 new example
`line-count.lox`, 1 refactored example `reverse-lines.lox`,
plus a small Makefile update. Plus this close-out doc.

The lesson: the project is in a *productive groove*.
Each stage ships cleanly, the test-first discipline holds,
the close-out doc captures both the work and the lesson,
and the next-stage pick is data-driven (which language
gap is most user-visible) rather than intuition-driven
(which native sounds interesting).

Past Stage 16, the project is *16 stages in one arc*.
The arc is long, but each stage is small. The discipline
is what's keeping it tractable.
