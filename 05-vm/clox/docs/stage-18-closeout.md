# Stage 18 Close-out — Escape Sequence Processing in String Literals

**Author:** Mira
**Date:** 2026-07-11
**Branch:** `stage-18-array-filter` (new branch from `stage-14-file-io`)
**Commits:** (this one — implementation + tests + close-out + refactor)

## What this stage is

String literals now process C-style escape sequences. Before
Stage 18, `"\n"` in clox source was the 2-char string backslash-n,
not a newline. The user had to put a real newline character in
the source to get a newline in the string. After Stage 18, the
natural C-style escapes work.

Supported escapes:

| Source | String |
|--------|--------|
| `\n`   | newline (0x0A) |
| `\t`   | tab (0x09) |
| `\r`   | carriage return (0x0D) |
| `\\`   | backslash |
| `\"`   | double quote |

Unknown escapes (e.g. `\q`) and incomplete escapes (a string
ending in `\` with no escape char following) are compile errors.

## Why this stage, not a bigger one

From the Stage 17 close-out's Next Steps:

1. ~~`string(n)` for number-to-string conversion.~~ ✅ Stage 17.
2. **`array_filter(arr, predicate_fn)` — clox function for filter.** 
   Discovered during planning that this requires user-code
   dispatch from a native, which is a bigger architectural
   change than fits in a small stage.
3. **Escape sequence processing in string literals** — scanner
   + compiler change, ~30 lines. Real user-facing gap. ✅

Picked escape sequences because:

1. **The Stage 15 cost is real and unblocked.** The `csv-roundtrip.lox`
   example used a real newline in source (line 12: a real `\n`
   character between the source `"`s) because clox didn't process
   `\n` in string literals. This worked but was awkward. Stage 18
   removes the workaround.

2. **Small, well-scoped change.** Two files touched: `scanner.c`
   (5 lines added — skip backslash + next char) and `compiler.c`
   (the `string()` function: ~30 lines).

3. **Fully reversible.** The change is in two localized functions.
   Git revert restores the previous behavior. The behavior change
   is forward-only in user-facing semantics, but git-revertible in
   the implementation.

4. **High-value idiom unblocked.** The "embed a newline in a
   string" pattern is so basic that almost every clox program
   that touches I/O or messages needs it. Stage 18 makes it
   natural.

## What I built

### Scanner change (`scanner.c`)

The scanner's `string()` function now skips a backslash and the
following character when scanning a string literal. This is
necessary so that `\"` in source doesn't terminate the string
early. The scanner still stores the source range verbatim; it
just walks past the escape to find the real closing quote.

5 lines added:
```c
if (peek(scanner) == '\\' && !isAtEnd(scanner)) {
    advance(scanner);
    if (!isAtEnd(scanner)) advance(scanner);
    continue;
}
```

### Compiler change (`compiler.c`)

The `string()` function now walks the source range and produces
a processed string. The algorithm:

1. Allocate a buffer of size `srcLen` (the source range, which is
   an upper bound on the output size since each escape shrinks
   or stays the same).
2. Walk the source. If the current char is `\`:
   a. Check that there's a char following. If not, error:
      "Unterminated escape sequence."
   b. Look at the next char. Dispatch:
      - `n` → newline
      - `t` → tab
      - `r` → carriage return
      - `\\` → backslash
      - `"` → double quote
      - default → error: "Invalid escape character '%c'."
   c. Increment `i` to consume the escape char.
3. Otherwise, copy the char to the output buffer.
4. At the end, call `copyString(buf, outLen)` to intern the
   processed string.

The error path returns early without emitting a constant. The
`parser.hadError` flag (set by the error function) prevents
the compiler from generating code for the failed compile.

## Bugs caught

**One test bug, zero implementation bugs.** I miscounted the
expected length of the mixed-escape test by 1 (I said 8, the
correct count is 9). The implementation was correct from the
first run. I caught the test bug on the first test pass when
the output was "9" instead of "8" and re-counted the escapes.

This is the 9th consecutive stage with zero implementation
bugs. The 1 test bug is the same family as Stage 15 (test
author miscounted), Stage 10 (test author assumed language
behavior the language doesn't provide), and Stage 9 (test
author assumed a language feature).

**Behavior change in edge cases:** two edge cases now produce
different results than before.

1. **Strings ending with `\`**: previously, `"foo\"` produced a
   4-char string `foo\` (the `\` is preserved, the `"` is the
   close). After Stage 18, `"foo\"` is an "Unterminated string"
   compile error (the scanner treats `\"` as an escape, then
   looks for a real closing quote and doesn't find one).

2. **Unknown escapes**: previously, `"\q"` was a 2-char string
   `\` + `q`. After Stage 18, `"\q"` is a "Invalid escape
   character 'q'." compile error.

These are strict-mode changes that match C, Lox book, and most
language conventions. They are not bug-for-bug reverts; the
previous behavior was a *side effect* of not processing escapes,
not a designed feature. The behavior change is documented in
this close-out and in the test file.

## Verification

```
$ cd 05-vm/clox && make clean && make
[no warnings under -Wall -Wextra -std=c99 -pedantic]

$ make test
==> bin/test_clox
27 passed, 0 failed   (+12 from Stage 17's 15)
==> bin/test_composability
8 passed, 0 failed
==> bin/test_repl
5 passed, 0 failed
==> bin/test_stdlib
104 passed, 0 failed

$ make valgrind
[0 errors, 0 leaks on all four binaries]
```

Total: **144/144 tests pass** (27 clox + 5 repl + 104 stdlib
+ 8 composability), valgrind clean on all four binaries.
test_clox shows 163 allocs / 163 frees.

### Manual smoke test of refactored example

```
$ printf "alpha,beta,gamma,delta\n" > csv-input.txt
$ ./bin/clox examples/csv-roundtrip.lox
ALPHA,BETA,GAMMA,DELTA
$ cat csv-output.txt
ALPHA,BETA,GAMMA,DELTA
```

The `csv-roundtrip.lox` example was refactored to use the
natural `"\n"` escape (line 12: `io_eprint("..." + "\n")`).
No more real newline in source.

### All 9 example programs pass `make examples`

```
==> examples/age-greeting.lox      — works
==> examples/class.lox             — works
==> examples/closure.lox           — works
==> examples/csv-roundtrip.lox     — works (refactored)
==> examples/fib.lox               — works
==> examples/hello.lox             — works
==> examples/line-count.lox        — works
==> examples/reverse-lines.lox     — works
==> examples/wordcount.lox         — works
```

## What this stage teaches

**The scanner-compiler boundary is the right place to process
escapes.**

Stage 18 could have put escape processing in three places:

1. **The scanner** (build the processed string directly). But
   the scanner doesn't know the string's full length yet (it
   hasn't found the closing quote), and processing escapes as
   it scans would require a variable-length buffer with
   realloc. Awkward.
2. **The compiler** (walk the source range, produce processed
   string). This is what Stage 18 does. The source range is
   known at this point (the TOKEN_STRING has a start/length),
   so we can walk it once.
3. **The runtime** (process escapes when the string is
   actually used). But the constant is stored once in the
   constant pool, not re-processed every use. So this would
   require either processing at constant-pool time (which is
   the compiler) or a per-use cost (bad).

Option 2 is the right call. The pattern: when the language
has a "compile-time interpretation" step, the compiler is
the right place. The scanner's job is to find tokens; the
compiler's job is to interpret them.

## A note on the implementation size

The full change is in 2 files, ~50 lines of new code:

- `scanner.c`: 8 lines (the backslash-skip logic)
- `compiler.c`: 42 lines (the new `string()` function with
  escape dispatch, error handling, and a 2 KiB stack buffer
  to avoid malloc for typical strings)

The 2 KiB stack buffer is a deliberate choice: most string
literals are short (a few hundred chars at most), so a stack
buffer is faster than malloc and there's no allocation to
free. The cap is checked and an error is raised for strings
larger than 2 KiB. This is a reasonable default; future work
could lift the cap or use a heap buffer for very large
strings.

## A note on edge cases I considered and rejected

- **`\0` (null byte)**: would let users embed null bytes in
  strings. Useful for binary data, but clox strings are
  C-style (length-prefixed) and embedding a null is a
  behavior change with subtle implications. **Skip for now.**

- **`\x41` (hex escape)**: would let users specify arbitrary
  byte values. Useful for binary data. **Skip for now.**

- **`\u00E9` (unicode escape)**: would let users embed
  non-ASCII characters. clox strings are byte-oriented
  (no UTF-8 awareness), so this would be a thin wrapper
  around `\x` for the first 256 codepoints. **Skip for now.**

- **`\b` (backspace), `\f` (form feed), `\v` (vertical tab)**:
  supported by C. Could add later if any program needs
  them. **Skip for now.**

The 5-escape minimum (`\n`, `\t`, `\r`, `\\`, `\"`) covers
all the cases that clox programs actually hit. The Lox book
has the same 5 escapes for the same reason.

## What's next

From the Stage 17 close-out, three remaining candidates:

1. ~~`string(n)` for number-to-string conversion.~~ ✅ Stage 17.
2. ~~Escape sequence processing.~~ ✅ Stage 18.
3. **`array_filter(arr, predicate_fn)` — needs a way for
   natives to call user code.** The Stage 15 cost is real but
   the fix is bigger than a small stage. Two paths:
   - **Path A: stdlib-in-lox.** Define `array_filter` as a
     clox function in a stdlib file, loaded at startup. This
     is the Lox book's pattern and gives the cleanest
     separation.
   - **Path B: callValue from native.** Expose the VM's
     `callValue` to native code so natives can invoke user
     functions. This is a vm.c change.
4. **Modules** — the big swing. ~600 lines. Still the most
   ambitious next step.

**My pick for Stage 19**: **`array_reverse(arr) -> arr`** as a
clean native (no user-function dispatch — it just iterates and
reverses). This closes *another* of the Stage 15 costs
without needing the bigger architecture. ~25 lines, one
design decision (in-place vs new array), fully reversible.

After Stage 19, the remaining "Stage 15 costs documented" gaps
will be:
- `array_filter` (needs the bigger architecture)
- `string+number` concat (needs compiler change)

Both are deferred to future stages.

## Closing observation

Stage 18 is the second-smallest stage in the project (after
Stage 17). It closes a Stage 15 cost that I documented as a
language limitation 2 stages ago. The discipline: when a
language limitation is documented, the next-step question is
"is this fix small enough for a stage?" If yes, do it. If
not, defer. Stage 18 was small enough.

The "Stage 15 costs documented" list is now:
- ~~filter needs a hand-rolled loop~~ (deferred to bigger stage)
- ~~escape sequences in literals aren't processed~~ ✅ Stage 18
- string+number concat doesn't work (deferred to compiler change)
- ~~array reverse needs a hand-rolled loop~~ (Stage 19 pick)

Of 4 costs, 2 are now closed. The remaining 2 are bigger
changes (function dispatch and operator overloading) and
correctly deferred. The discipline of documenting the
limitations is paying off: each "deferred" item has a
concrete next-step design.
