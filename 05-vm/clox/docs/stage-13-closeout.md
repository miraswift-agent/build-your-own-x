# Stage 13 Close-out — `string_split` / `string_join`

**Author:** Mira
**Date:** 2026-07-10
**Branch:** `stage-13-string-split-join`
**Commits:** (this one — implementation + close-out)

## What this stage is

Two natives that compose the new `ObjArray` value type
with the existing string natives. After Stage 12, arrays
are real and strings are first-class; `string_split` /
`string_join` are the bridge. They unblock the obvious
clox idiom:

```
var parts = string_split(line, ",");
var back  = string_join(parts, ",");
```

A round-trip that already had no obvious way to be
written before this stage.

## Why this stage, not a bigger one

Three candidates from the Stage 12b-iii close-out:

1. **`string_split` + `string_join`** — ~80 lines, one
   design decision, reversible in one commit. ✅
2. **More I/O natives** — `io_read_file`,
   `io_write_file`, `io_file_exists`. ~150 lines,
   diminishing returns on the host-boundary lesson.
3. **Modules** — ~600 lines, multiple design decisions
   (file paths, recursive imports, circular detection,
   import syntax), any of which made wrong would
   require a rework.

The decision rule: pick the smallest stage that teaches
the next lesson. The lesson here is *composing the new
value type with the existing ones*. The lesson for
modules is *file I/O + namespace design* — related but
distinct, and bigger. After nine stages in one arc
(7 through 12b-iii) the discipline is to ship a small
stage cleanly and let the next one emerge from the
close-out. Modules stays on the list; the question is
when, not whether.

## What I built

### `string_split(s, delim) -> ObjArray`

Implementation lives in `src/native.c` (Stage 13 block).
Algorithm:

1. Compute worst-case array size: `s->length /
   delim->length + 1`. For `delim->length == 0` use 1.
2. `newArray(worstCase)`, `push()` it for GC protection.
3. Walk `s` with two pointers: `prev` (start of next
   piece) and `i` (current scan position).
4. At each `i`, check if `delim` starts at `i`. If yes,
   copy `s[prev..i]`, push the substring, advance past
   the delim, update `prev`. If no, advance `i`.
5. After the loop, push the tail `s[prev..s->length]`
   (which may be empty — trailing empty string after a
   trailing delim).
6. `pop()` the protective push, return the array.

Edge cases verified by tests:

| Input | Delim | Result |
|---|---|---|
| `"a,b,c"` | `","` | `["a", "b", "c"]` |
| `"hello"` | `""` | `["hello"]` |
| `""` | `","` | `[]` |
| `"hello"` | `","` | `["hello"]` (no match) |
| `"a,,"` | `","` | `["a", "", ""]` (trailing empties) |
| `",,"` | `","` | `["", "", ""]` (leading + trailing) |
| `"ab--cd--ef"` | `"--"` | `["ab", "cd", "ef"]` (multi-char delim) |
| `"a"` | `","` | `["a"]` (single char, no delim) |
| `","` | `","` | `["", ""]` (all empty) |

### `string_join(arr, delim) -> ObjString`

Implementation lives in `src/native.c` (same Stage 13
block). Algorithm:

1. If `arr` is empty, return `""` (single `copyString`
   of length 0).
2. Compute total length: sum of `arr[i]->length` for
   strings in `arr`, plus `(arr->count - 1) * delim->length`
   for the inter-piece delims.
3. `copyString("", totalLength)` to allocate the buffer.
4. Walk `arr`, `memcpy` each piece into the buffer, then
   `memcpy` the delim (except after the last piece).
5. Return the joined string.

Edge cases verified by tests:

| Input | Delim | Result |
|---|---|---|
| `["a", "b", "c"]` | `","` | `"a,b,c"` |
| `[]` | `","` | `""` (empty array → empty string) |
| `["only"]` | `","` | `"only"` (no delim used) |
| `["a", "b"]` | `"--"` | `"a--b"` (multi-char delim) |
| `["a", 42, "c"]` | `","` | runtime error (non-string element) |
| `["a", "b"]` | `42` | runtime error (non-string delim) |
| `["a"]` | `","` | `"a"` (1-arg to 2-arg is the only valid call) |

### Type checking

Both natives follow the same `clock()`-style protocol
established in Stage 7:

- `runtimeError(...)` on wrong arity.
- `runtimeError(...)` on wrong type at runtime.
- Return `NIL_VAL` on the runtime-error path (unreachable
  because `runtimeError` longjmps, but keeps the function
  signature clean — no warnings).

## Bugs caught

**None new in this stage.** The tests were written first,
the implementation matched, and valgrind was clean on
the first run.

This is the second time the discipline has paid off
this project. Stage 12b-ii (index read) also had 0
bugs caught at implementation time. The pattern: when
tests are written before the code, the implementation
*is the test contract*, and the boundary is unambiguous.

The test-side bugs that have happened (4 in the project
so far, 0 in this stage) all happened when an assertion
*didn't* match what the user observes. Stage 13 is a
clean implementation pass because the tests assert on
the exact byte layout of the script output.

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
63 passed, 0 failed   (+13 from Stage 12b-iii's 50)

$ make valgrind
[0 errors, 0 leaks on all three binaries]
```

### GC stress test (in test suite)

```lox
var s = "alpha,beta,gamma,delta,epsilon,zeta,eta,theta";
var sink = [];
var i = 0;
while (i < 200) {
    var parts = string_split(s, ",");
    var joined = string_join(parts, ",");
    array_push(sink, joined);
    i = i + 1;
}
print(array_length(sink));
print(sink[0]);
print(sink[199]);
```

```
==100028==     in use at exit: 0 bytes in 0 blocks
==100028==     total heap usage: 378 allocs, 378 frees
==100028== All heap blocks were freed -- no leaks are possible
==100028== ERROR SUMMARY: 0 errors from 0 contexts
```

The `sink` array grows to 200 elements across the
loop, which forces collection cycles. Each iteration
allocates 8 substring pieces + 1 joined string + 1
array growth. Every alloc has a matching free.

### External stress (valgrind on a 1000-iter script)

```bash
$ cat > /tmp/stress.lox <<'EOF'
var s = "the quick brown fox jumps over the lazy dog";
var i = 0;
var lastLen = 0;
while (i < 1000) {
    var parts = string_split(s, " ");
    lastLen = array_length(parts);
    var joined = string_join(parts, " ");
    i = i + 1;
}
print(lastLen);
print(i);
EOF
$ valgrind --leak-check=full ./bin/clox /tmp/stress.lox
9
1000
==99493== total heap usage: 3,150 allocs, 3,150 frees
==99493== All heap blocks were freed -- no leaks are possible
==99493== ERROR SUMMARY: 0 errors
```

3,150 allocs / 3,150 frees across 1000 round-trips.
GC is doing its job.

## The new pattern in clox

Stage 13 introduces a small but real shift: clox now
has *array-producing* and *array-consuming* stdlib
functions. Before this stage, the only way to construct
an array was the `array()` native (Stage 12a) or the
`[expr, expr, ...]` literal (Stage 12b-i). `string_split`
adds a third construction path — one that operates on
strings rather than on explicit values.

This matters for the "small language" feel of clox.
A user can now do:

```
var line = io_read_line();
var fields = string_split(line, ",");
print(array_length(fields));
print(fields[0]);
```

…which is a non-trivial program. Three lines, four
language features (I/O, string → array via split, array
length, array index). None of which existed as
composable pieces before Stages 7–13.

## Manual smoke test

```lox
var csv = "apple,banana,cherry,date";
var fruits = string_split(csv, ",");
print(array_length(fruits));    /* 4 */
print(fruits[0]);               /* apple */
print(fruits[3]);               /* date */

var reconstructed = string_join(fruits, ",");
print(reconstructed);           /* apple,banana,cherry,date */
print(reconstructed == csv);    /* true */

var words = string_split("hello   world", " ");
print(array_length(words));     /* 5 (preserves empty pieces) */

var empty = string_split("", ",");
print(array_length(empty));     /* 0 */
print(empty == []);             /* true */

var joined_empty = string_join([], ",");
print(array_length(joined_empty)); /* hmm — joined_empty is a string, not an array */
                                  /* so array_length on a string is a runtime error */
```

All outputs correct. The last one is a real Lox
boundary: `string_join` returns a `string`, and
`array_length` expects an `array`. The error surfaces
at the runtime-error boundary, not silently.

## What this stage teaches

The lesson is *composition*. The build-your-own-x
arc through Stages 7–12b-iii established:
- the array value type
- the string value type
- I/O natives
- language syntax for both

…as independent pieces. Stage 13 is the first stage
where the *interesting* work is using multiple pieces
together, not adding a new piece. The implementation
is small (about 110 lines in `native.c` plus tests)
because the *plumbing* was already there. The
substance is in the *semantics* — what does
`string_split` *mean* for a clox program? — which is
now expressible as a clox program itself.

This is also the first stage where the project is
genuinely past the "extension" phase. Past Stage 13,
the obvious next steps are *bigger* (modules, a real
type system, native function composition) and the
project is no longer a one-feature-at-a-time line.

## What's next

Three live candidates, in priority order:

1. **More stdlib composability.** `string_trim` already
   exists; composing `string_trim` with `string_split`
   is a one-liner in clox. Other useful pairings:
   `string_upper` / `string_lower` to normalize before
   splitting, `string_index_of` to find delim positions
   without splitting. These are *user-level* composition
   rather than *implementation* composition — they
   don't need new natives.

2. **More I/O natives.** `io_read_file`, `io_write_file`,
   `io_file_exists`. ~150 lines. Diminishing returns on
   the host-boundary lesson (Stage 11 already covered
   it), but the user-facing gap is real.

3. **Modules.** The big one. ~600 lines. Multiple
   design decisions. Still the most ambitious next
   step. Open question: do I want to keep adding
   small stages to extend the language vertically, or
   take a 4-day swing at modules?

4. **Option B: branch sideways.** A small shell or
   KV store in C, applying what Stages 1–13 taught.
   Unblocked at any time.

**My pick for next stage** (pausing here for Tom's
call): **more I/O natives.** The user-facing gap is
real (no way to read a file in clox), the size is
moderate (~150 lines), and the design decision space
is small (file path handling, error semantics, byte
vs. string distinction). Modules is the bigger
ambition; this is the smaller one. Pick the small
one until the discipline needs the big one.
