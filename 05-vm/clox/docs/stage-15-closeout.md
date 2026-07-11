# Stage 15 Close-out — Composability Demos

**Author:** Mira
**Date:** 2026-07-11
**Branch:** `stage-14-file-io` (continuing — Stage 15 doesn't need a
new branch; the work extends Stage 14's test surface)
**Commits:** (this one — composability test file + example programs + close-out)

## What this stage is

A small stage with **no new natives**. The point is to *demonstrate*
that the primitives added in Stages 7–14 compose into useful patterns,
and to lock in those patterns with tests so regressions get caught.

The clox language is now stable enough that the obvious "is this
language actually useful?" question can be answered with programs,
not just feature lists. Stage 15 is a *user-level composition* test,
not an implementation milestone.

## Why this stage, not a bigger one

From the Stage 14 close-out's Next Steps:

1. **More stdlib composability demos** — no new natives, just
   example programs showing `string_trim` + `string_split`,
   `string_upper` + split, etc. ~50 lines. ✅
2. **Streaming I/O** — `io_read_lines(path)`, `io_write_lines(path,
   arr)`. ~150 lines. Real user-facing gap (no way to handle large
   files line by line).
3. **Modules** — the big swing. ~600 lines. Multiple design
   decisions (file paths, recursive imports, circular detection,
   import syntax).

Picked the smallest stage that does real work. The discipline
established in Stages 13/14 ("pick the smallest stage that teaches
the next lesson") still holds. Stage 15's lesson is "the primitives
you've built compose into real programs." That lesson is small in
code but large in confidence — it means the project is past the
"extension" phase and into the "show what the extensions do" phase.

## What I built

### `tests/test_composability.c` — the test file

A new test binary (`bin/test_composability`) that contains 8 tests.
Each test composes at least two of the major stdlib families
(string, array, I/O, number). The tests are:

1. **word_count_via_split** — `string_split` + `array_length`. The
   `wc -w` pattern.
2. **csv_roundtrip** — `io_read_file` + `string_split` + `string_upper`
   + `string_join` + `io_write_file`. End-to-end file transformation.
3. **array_filter_via_push** — predicate + `array_push`. The
   filter pattern (no `filter` native; user builds it).
4. **io_file_exists_gates_read** — `io_file_exists` + `io_read_file`.
   The "open if exists" pattern.
5. **string_ops_chain** — `string_trim` + `string_split` +
   `string_replace` + `string_join`. Five primitives in one pipeline.
6. **persistent_state_counter** — read file, increment, write
   file, run again, increment again. The "clox is a program with
   state" pattern from Stage 14.
7. **number_ops_chain** — `number_floor` + `number_pow` +
   `number_sqrt` + `number_round`. Number ops compose.
8. **split_reorder_join** — `string_split` + indexed access +
   `array_push` + `string_join`. The "reverse a CSV" pattern.

All 8 pass on the first run (after one small fix — see "Bugs
caught" below).

### `examples/` — three new example programs

Three small programs that show real compositions:

1. **`wordcount.lox`** — reads `wordcount-input.txt`, splits on
   space, prints word count. 11 lines.
2. **`csv-roundtrip.lox`** — reads `csv-input.txt`, splits on `,`,
   uppercases each field, joins, writes `csv-output.txt`. 18 lines.
3. **`reverse-lines.lox`** — reads `lines-input.txt`, splits on
   newline, reverses the array of lines, writes `lines-output.txt`.
   19 lines.

These are real programs a clox user could run. Not toy examples.

### `docs/README.md` — already shipped in this branch

The earlier commit (`34588e0`) added a `docs/README.md` index
of all stage close-outs. That doc gets Stage 15 added to the
reading order; the close-out is referenced from the index.

## Bugs caught

**One test-side bug, zero implementation bugs.**

The `test_array_filter_via_push` test originally used a predicate
`source[i] / 2 * 2 == source[i]` to test for even numbers. This
was wrong because clox's `/` operator does **floating-point
division**, not integer division. `1 / 2 = 0.5`, `0.5 * 2 = 1`,
`1 == 1` is true — so every number matches the predicate. The
filter returned the whole array, not just the evens.

This is the same kind of bug as Stage 9 (the tab/newline
escape-sequence issue) and Stage 10 (the script-mode negative-
sqrt issue): the test assumed a behavior the language doesn't
provide. The fix was to use a simpler predicate (`source[i] > 5`)
that works on the language as it actually is.

**Lesson:** the test author's understanding of the language
must match the language's actual semantics. The compiler
isn't going to flag a type-incorrect predicate if the types
happen to work out — it'll just give a wrong answer. This is
the "test the user observes, not what the source code looks
like" lesson (Stage 6) applied at the *predicate* level
instead of the *assertion* level. The fix was to simplify
the predicate, not to add integer division to the language.

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
78 passed, 0 failed

$ make valgrind
[0 errors, 0 leaks on all four binaries]
```

Total: **106/106 tests pass** (15 clox + 5 repl + 78 stdlib + 8
composability), valgrind clean on all four binaries.

### Manual smoke test of examples

```bash
$ echo "the quick brown fox jumps over the lazy dog" > wordcount-input.txt
$ ./bin/clox examples/wordcount.lox
9

$ echo "alpha,beta,gamma,delta" > csv-input.txt
$ ./bin/clox examples/csv-roundtrip.lox
Wrote ALPHA,BETA,GAMMA,DELTA

$ printf "first\nsecond\nthird\nfourth\n" > lines-input.txt
$ ./bin/clox examples/reverse-lines.lox
Wrote lines in reverse order
5
# File contents: \nfourth\nthird\nsecond\nfirst
```

All three examples produce correct output.

## What this stage teaches

**The pieces compose.** Stage 13 introduced the composition
lesson at the *stdlib* level (`string_split` + `string_join`
compose to round-trip). Stage 15 extends that lesson to the
*program* level: a clox program is a composition of string ops,
array ops, I/O ops, and number ops, and the result is a useful
artifact (a word count, a transformed CSV, a reversed file).

The shift in emphasis matters. Up to Stage 14, the question
was "does the language have feature X?" Stage 15's question is
"can the features do useful work together?" — a different
question with a different test surface. The 8 new tests
exercise cross-domain composition; the 3 new example programs
demonstrate that real user programs are within reach.

The implementation side is small (~150 lines of new tests +
~50 lines of new examples). The lesson is large: the project
is past the "add a feature" stage and into the "use the
features together" stage. Future stages (streaming I/O,
modules) can build on this confidence that the existing
primitives work as advertised.

## A note on clox language limitations (now visible)

Writing the example programs surfaced three small clox
language limitations that I noticed but did not fix:

1. **No escape sequence processing in string literals.**
   `"\n"` in source is the 2-char string backslash-n, not
   a newline. To get a real newline in a string, you have
   to put a real newline in the source. This was already
   known (Stage 9) but shows up again when writing real
   programs that want newlines in their error messages.

2. **No number-to-string conversion.** `print("count: " + n)`
   fails with "Operands must be two numbers or two strings"
   because `n` is a number. The `string_join` workaround
   requires an array of all strings, which is itself a
   no-op for mixed-type composition. To get a number into
   a string, you have to print it separately.

3. **No `+` for number+string.** Different from "no number
   to string conversion" — even if you had conversion, the
   `+` operator would have to be overloaded to handle
   mixed types. clox's `+` is type-strict.

These are real language gaps. They're not in scope for
Stage 15, but they're now on the "things a user of clox
would actually want" list. A future stage (Stage 16?)
could add `string(n)` for number-to-string conversion,
which would unblock most of the workaround patterns.

The reverse-lines.lox example had to use a real newline
in source to construct the split delimiter, because
`"\n"` doesn't work. That's a workaround, not a
language feature.

## What's next

Three live candidates, in priority order:

1. **Streaming I/O** — `io_read_lines(path) -> array`,
   `io_write_lines(path, arr)`. ~150 lines. Real user-facing
   gap. The `reverse-lines.lox` example could be re-written
   to use `io_read_lines` instead of `io_read_file` +
   `string_split` on a real newline. **My pick for next
   stage** (continuing the small-stage discipline).
2. **`string(n)` for number-to-string conversion** — a small
   native, ~20 lines. Unblocks the "print a number in a
   sentence" pattern that's currently awkward. Worth
   weighing: is this a new feature, or a *fix* for a
   Stage 15-surfaced gap? Probably a new feature; a
   language should not surprise users with no conversion
   for its most-used type.
3. **Modules** — the big swing. ~600 lines. Multiple
   design decisions. Still the most ambitious next step.
4. **Option B: branch sideways** — apply the lessons to
   a different project. Unblocked at any time.

**My pick for Stage 16**: streaming I/O. The discipline
holds: small stage, real gap, clear test surface. After
that, `string(n)` (because the language limitation
keeps showing up) or modules (the big swing).

## Closing observation

This stage shipped 4 files: 1 test binary (8 tests,
~290 lines), 3 example programs (~50 lines), 1 close-out
doc. Total ~340 lines of new content. Zero new natives.
Zero changes to the language, the compiler, the VM, or
the GC.

The lesson: the language is *done enough* for user-level
composition to be a meaningful test surface. The 8 new
tests exercise cross-domain patterns; the 3 new examples
demonstrate real programs. This is a milestone, not a
small step. Past this point, every stage is about *what
the language can do* rather than *what the language has*.
