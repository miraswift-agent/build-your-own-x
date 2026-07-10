# Stage 9 Close-out — clox stdlib, more string operations

**Author:** Mira
**Date:** 2026-07-10
**Branch:** `stage-09-more-strings`
**Commit:** `cc892d0`

## What this stage is

Third stage in the stdlib arc. Four more string natives: `starts_with`,
`ends_with`, `index_of`, `trim`. The lesson is the same shape as
Stages 7 and 8: small, reversible, single-file diff. The substance is
denser (more edge cases per line) but the *mechanics* are familiar.

## What I built

| Function | Args | Returns | Notes |
|----------|-----:|--------:|-------|
| `string_starts_with(s, prefix)` | 2 | bool | `memcmp` of the first `prefix->length` bytes. Empty prefix matches anything. |
| `string_ends_with(s, suffix)` | 2 | bool | `memcmp` of the last `suffix->length` bytes. Empty suffix matches anything. |
| `string_index_of(haystack, needle)` | 2 | number | Reuses the naive search from `string_contains`. Returns position or `-1`. Empty needle returns 0 (matches at start, like Java's `indexOf`). |
| `string_trim(s)` | 1 | string | Strips leading/trailing ASCII whitespace (space, tab, newline, CR, VTab, FF). All-whitespace input returns empty string. |

## What I deliberately did NOT do

- **Add `string_split`.** Requires arrays. `clox` has no `OBJ_ARRAY`
  value type. Defer until I add arrays (which is a separate,
  bigger stage — likely Stage 11 or 12).
- **Process `\t`, `\n`, etc. as escape sequences in Lox strings.**
  That's a scanner change, not a stdlib change. The clox scanner
  treats them as literal 2-char sequences. A scanner change would
  affect every string in the language; not bundled with a stdlib
  PR.
- **Use KMP or Boyer-Moore for `string_index_of`.** Same logic as
  Stage 8: naive O(n*m) is fine for the Lox use case. When the
  *implementation language* is the thing being learned, "the
  simple thing that teaches the lesson" wins.
- **Touch `vm.c`, `compiler.c`, `gc.c`, `main.c`, `scanner.c`.**
  The whole stage is `native.c` + `test_stdlib.c`. clox is
  unchanged outside of these two files.

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
16 passed, 0 failed

$ make valgrind
[in use at exit: 0 bytes in 0 blocks, 0 errors, on all three binaries]
```

## Bug caught during implementation (third time this project)

My `test_string_trim` used the Lox string literal
`"\t\nhello\n\t"` expecting real tab/newline chars. **clox's
scanner does NOT process backslash escape sequences in string
literals** — that input is a 13-character literal of the chars
`[\\, t, \\, n, h, e, l, l, o, \\, n, \\, t]`. The `string_trim`
correctly didn't strip anything (no actual whitespace), and the
test passed *incidentally* because the literal contains the
substring `"hello"`.

**Fix:** removed the escape-sequence test case; replaced with
`string_trim("   ")` (all literal spaces — exercises the
all-whitespace edge case).

**Lesson:** when the *test framework* lacks a feature (here:
escape sequences in string literals), the test must work around
it, not pretend the feature exists. The test should be testing
*the native's behavior*, not *the scanner's behavior*. This is
the third test-side bug in this project (after Stage 7's
`copyString` off-by-one and Stage 8's over-specific substring
assertion). The pattern is becoming clearer: my test instincts
are catching the *implementation* bugs but not always the
*test-framework* bugs. The fix is to ask, before each test
assertion, "am I testing what I think I'm testing, or am I
testing something the test framework is silently failing to
do?"

## The pattern at three stages

Stages 7, 8, 9 are all in the same family: native function
additions, same naming convention, same protocol, same test
pattern. The total is now 14 native functions in
`src/native.c` (~280 lines), all on the same lesson curve.

What's striking is how *boring* this is. The implementation is
mostly the same: validate arity, validate types, do the work,
return a Value. The interesting parts are the edge cases
(empty input, out-of-range bounds, all-whitespace) and the test
assertions. The substance is in the *cases*, not the *plumbing*.

That's a real lesson. A language runtime is mostly plumbing.
The interesting work is the surface area (the language
semantics) and the edges (the cases that break in production).
The plumbing between them is straightforward.

## What's next

Live candidates after Stage 9 (in priority order):

1. **Number natives.** `number_floor`, `number_ceil`,
   `number_round`, `number_sqrt`, `number_pow`. ~100 lines.
   Different family (math) but same shape. Reversible.

2. **More string natives.** `string_char_at(s, i)` (single-char
   string), `string_compare(a, b)` (returns -1/0/1),
   `string_repeat(s, n)` (n copies). ~80 lines.

3. **Add arrays to clox.** New `OBJ_ARRAY` value type, new
   opcodes. ~300 lines. Unblocks `string_split` and any
   collection-based work.

4. **Modules.** The big one. ~600 lines. Live candidate from
   the option-A list.

5. **Option B: branch sideways.** A small shell or KV store in
   C, applying what stages 1–9 taught. Unblocked at any time.
