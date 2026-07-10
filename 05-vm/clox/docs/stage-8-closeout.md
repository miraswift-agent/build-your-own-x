# Stage 8 Close-out — clox stdlib, more string operations

**Author:** Mira
**Date:** 2026-07-10
**Branch:** `stage-08-string-stdlib`
**Commit:** `bd324f6`

## What this stage is

Continuation of the Stage 7 stdlib. Three more native functions in the
same family: `string_substring`, `string_contains`, `string_replace`.

## Pick rationale (shorter this time)

Stage 7 established the rhythm: a small, reversible stage that
*teaches the next lesson*. Stage 8 is the same shape, plus one new
micro-lesson: **assertions on multi-line test output need to match
the exact layout**. The replace-test bug I caught while writing the
tests (see "Bug caught" below) is a small instance of a more general
discipline — when a test asserts on `contains(out, "...")`, the
substring has to match the *actual* byte layout, not the
*approximate* shape.

## What I built

Three new native functions in `src/native.c`:

| Function | Args | Returns | Notes |
|----------|-----:|--------:|-------|
| `string_substring(s, start, end)` | 3 | string | `s[start..end)`. Out-of-range start/end is clamped. Empty when `start > end`. |
| `string_contains(haystack, needle)` | 2 | bool | O(n*m) naive search. Empty needle is always contained. |
| `string_replace(s, old, new)` | 3 | string | Replaces first occurrence. If `old` not present, returns a fresh copy of `s` (caller always gets a new `ObjString`). |

## What I deliberately did NOT do

- **Add `string_starts_with`, `string_ends_with`, `string_index_of`.** Three more small natives that fit the same shape. Saved for a later stage. *Not* bundling because Tom's "iterate" instruction favors small.
- **Use KMP or Boyer-Moore for `string_contains`.** Naive O(n*m) is fine for the Lox use case (small strings, called in user code not in tight loops). When the *implementation language* is the thing being learned, "the simple thing that teaches the lesson" beats "the production-grade thing." KMP is a different lesson; if I want it, I can ship it as Stage 9 or 10.
- **Add arrays to clox.** Same blocker as Stage 7: clox has no `OBJ_ARRAY` value type, so `string_split` isn't implementable. `string_replace` returns a `string` (not an array), so it works without arrays. Arrays are still a separate stage, not bundled here.
- **Touch `vm.c`, `compiler.c`, `gc.c`, `main.c`.** The whole stage is `native.c` + `test_stdlib.c`. clox is unchanged outside of these two files. **Reversibility signal: `git revert bd324f6` rolls this back cleanly.**

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
12 passed, 0 failed

$ make valgrind
[in use at exit: 0 bytes in 0 blocks, 0 errors, on all three binaries]
```

Manual smoke:
```
$ ./bin/clox /tmp/clox_s8_smoke.lox
=== substring ===
World
abc
abc
=== contains ===
true
false
true
false
=== replace ===
Hello, there!
aaXbcc
Hello
```

## Bug caught during implementation

My replace-test assertion checked for the substring `"hello\nhello"`
in the output, expecting the third print to be `"hello"` on a new
line after the second print. Actual output is
`"hello there\nbaa\nhello\n"` — the `"hello"` from the unchanged
third print is at the end, not preceded by another `"hello"`. The
assertion was wrong, not the implementation.

**Fix:** changed the assertion to `contains(out, "hello\n")` (just
the unchanged line). The implementation is correct; the test was
over-specified.

**Lesson:** assertions on multi-line test output need to match the
*exact* byte layout, not the *approximate* shape. A test that
asserts "the output mentions 'hello' twice" would have passed; a
test that asserts "the output mentions 'hello\nhello'" was too
specific. The right granularity was somewhere in between. This is
the second time this kind of bug has surfaced (the first was the
`copyString` off-by-one in Stage 7), and it's the kind of thing
that gets easier to spot with practice.

## The lesson-to-line ratio, one stage later

Stage 7 was 7 natives in ~100 lines. Stage 8 was 3 natives in ~70
lines. The lesson-to-line ratio is *higher* in Stage 8 because the
substance is denser (substring/contains/replace are real algorithms,
whereas Stage 7's abs/min/max were just dispatch). But the
*reversibility* is the same: one revert, no language changes, no
new opcodes. That's the rhythm Tom asked for, and it's working.

## What's next

Live candidates after Stage 8 (in priority order):
1. **More string natives, same shape.** `string_starts_with`, `string_ends_with`, `string_index_of`, `string_trim`, `string_split` (the last requires arrays). ~150 more lines.
2. **Add arrays to clox.** This unblocks `string_split` and any work involving collections. ~300 lines, with new opcodes.
3. **More number natives.** `number_floor`, `number_ceil`, `number_round`, `number_sqrt`, `number_pow`. ~100 lines.
4. **More string-ish natives.** `string_compare(a, b)` (returns -1/0/1), `string_char_at(s, i)` (returns a single-char string). ~50 lines.
5. **Eventually: modules.** Still the right pick when the stdlib is fully built out and I'm ready to think about file I/O.
