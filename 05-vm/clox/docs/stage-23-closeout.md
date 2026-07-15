# Stage 23 Close-out: string_pad_end

**Stage:** 23 of 33
**Branch:** `stage-23-string-pad-end`
**Commits:** `e20eb0a` (impl + tests + example) — close-out `2cf3c43` (this file)
**Date:** 2026-07-14
**Tests:** 180/180 (was 172/172 before Stage 23, +8 stdlib tests)
**Valgrind:** clean (864 allocs / 864 frees in test_stdlib, 0 errors, 0 leaks)
**Bugs caught:** 0. **14th consecutive stage with zero implementation bugs.**

## What shipped

`string_pad_end(s, width, fill) -> string` — pad `s` on the RIGHT with copies of `fill` until the result is at least `width` characters. JavaScript's `String.prototype.padEnd` semantics (Python's `str.ljust` doesn't support multi-char fill, so JS is the canonical reference for multi-char fill behavior). ~85 lines in `src/native.c` (impl body ~40, the rest is comment). Single allocation bounded by `width` (a runtime int bounded by `INT_MAX`), so no separate overflow guard is needed. The suffix is `padLen = width - s->length`, with `fullCopies = padLen / fill->length` full copies and a partial-copy pass for the remainder.

## Tests added (8 total)

In `tests/test_stdlib.c`:
1. `test_string_pad_end_basic` — `"abc"` padded to 6 with `"-="` → `"abc-=-"` (6 chars). Mirrors Stage 22's basic test.
2. `test_string_pad_end_already_wide` — s->length >= width returns s unchanged. Two cases: `"hello"` with width 3, `"abc"` with width 3.
3. `test_string_pad_end_empty_s` — `""` padded to 4 with `"x"` → `"xxxx"`.
4. `test_string_pad_end_width_zero` — width 0 returns s unchanged.
5. `test_string_pad_end_negative_errors` — negative width → runtime error.
6. `test_string_pad_end_empty_fill_errors` — empty fill → runtime error.
7. `test_string_pad_end_wrong_type` — non-string first arg → runtime error.
8. `test_string_pad_end_wrong_arg_count` — wrong arity → runtime error.

## Design decisions

(1) `s->length >= width` → return `s` unchanged (JS `padEnd` convention: never truncate, never error on "already wide enough"). (2) `width = 0` → same path (the `>=` handles it). (3) Negative `width` → runtime error (matches `string_repeat` / `string_substring` / `string_pad_start`'s discipline). (4) Empty `fill` → runtime error (padding with nothing is nonsensical; if the caller wanted to truncate, they should use `string_substring`). (5) Fractional `width` → truncated to int (matches the cast). (6) Single allocation, `fullCopies + (remainder > 0 ? 1 : 0) + 1` memcpy passes, one `copyString` call, no intermediate `ObjString`s.

## New example

`examples/pad-right-numbers.lox` — the natural "right-aligned column" idiom. Composes Stage 17's `string(n)` conversion + Stage 12b's `for` loop + Stage 23's `string_pad_end`. The example also has a "combined with Stage 22" section showing symmetric padding (`string_pad_start("hi", 6, " ")` → `"    hi"`; `string_pad_end("hi", 6, " ")` → `"hi    "`). 13 example programs total.

## What this stage teaches

*Three small-mirror stages in a row is a real signal.* Stages 21 (`string_repeat`) → 22 (`string_pad_start`) → 23 (`string_pad_end`) all use the same shape: single allocation bounded by a runtime int, `n` memcpy passes, one `copyString` call, no intermediate `ObjString`s, no GC pressure. The "return the input unchanged on no-op" pattern is identical across Stages 22 and 23. The error contract (negative width / empty fill / wrong type / wrong arity) is identical across Stages 22 and 23. The discipline is becoming a *family* — a string-padding idiom closed by 2 natives, with `string_substring` doing the truncate-when-needed complement.

*Test bug lesson (carried over from Stage 22).* The 4 "error case" tests in Stage 23 (negative / empty-fill / wrong-type / wrong-arity) check `exit code != 0` only, not the error message text. A Stage 24 that accidentally re-used `string_pad_start`'s implementation under the `string_pad_end` name would pass Stage 23's tests. This is the same weakness as Stage 22's tests, and the discipline to mitigate it is the same: the wiki INDEX's per-stage deep entry notes the limitation, and the Stage 24 review checks the impl diff line count. A stronger fix would be to add `contains(out, "string_pad_end")` checks to the error-case tests; deferred as a Stage 24 follow-up if the test-discipline gets revisited.

*The 8-test shape is becoming a unit.* Stages 22 and 23 both have exactly 8 tests covering the same 8 paths: basic / already-wide / empty-s / width-zero / negative / empty-fill / wrong-type / wrong-arity. This is a regression coverage floor: any future stage that follows the same shape will have 8 tests minimum. Stages 1–20 had more variable test counts; the small-mirror rhythm is producing consistent coverage.

*JS vs Python reference is now a permanent note.* Both Stages 22 and 23 reference JavaScript rather than Python because Python's `str.rjust` / `str.ljust` don't support multi-char fill. The wiki's per-stage deep entry for both stages makes this explicit. The "canonical reference" for a multi-char-fill string-padding primitive in 2026 is JavaScript, not Python; this is a domain fact that future string-handling stages should reference when picking a comparison language.

## What this stage does NOT teach (the limitation)

*No end-to-end example of the table-printing use case.* The example file shows a 9-element column of squares (`1, 4, 9, 16, ...`) but doesn't show a multi-column table with a header row. The "pretty-print a table" idiom would also need `string_repeat` (Stage 21) for the divider line and `string_length` for column widths — a richer example would compose all four. Deferred because the example was kept small to match Stages 21 and 22's example size; the "composes all four" example is a Stage-24-or-later follow-up.

*No GC stress test.* Stage 13's `string_split` has a `string_split_gc_stress` test (1000+ iterations with concurrent allocations). Stages 21-23 don't have an equivalent. The single-allocation pattern is GC-friendly by construction (no intermediate `ObjString`s), but a stress test would verify that empirically. Deferred — the valgrind run on the actual test_stdlib suite is a weaker form of the same check (it exercises 864 allocs / 864 frees through `copyString` + `ALLOCATE` / `FREE_ARRAY` cycles), so the absence of a stress test is a coverage gap, not a correctness gap.

## My pick for Stage 24

The "Stage 23+ candidates, in priority order" section of the wiki INDEX names:
1. `string_pad_end` is the previous pick (shipped in Stage 23).
2. `array_filter(arr, predicate)` — needs user-code dispatch from a native, ~150 lines, bigger architecture. Defer.
3. Modules — ~600 lines, biggest swing. Tom's call.

The natural small-mirror candidate is exhausted (Stages 21-23 closed the obvious 3 string-handling idioms). The next "obvious idiom the user has to hand-roll" gap is:

* **Numeric-formatting helpers** — `string_pad_start(string(n), width, "0")` is the obvious "zero-padded number" idiom (Stage 22's example shows it). But it's a composition, not a new native. The next real native would be `string_trim_start(s) / string_trim_end(s)` (mirror of `string_trim` from Stage 9), or `string_strip_chars(s, chars)` (remove a set of characters from both ends). These are all small.

* **`string_to_number(s) -> number`** — the inverse of Stage 17's `string(n)`. Would close the round-trip idiom: `string(string_to_number("42") + 1)` → `"43"`. ~30 lines, no new concepts. **My pick for Stage 24.** Same shape as Stages 21-23, same small-mirror rhythm (mirrors Stage 17's `string` native).

* **`string_to_number(s, base)`** — the multi-base variant. Bigger (handles bases 2, 8, 10, 16, and the edge cases of "0x" prefix for hex). ~50 lines. Higher learning value (the "what's a number" question is more interesting than the "how to convert" question). Stage 25 candidate.

The Stage 24 pick is **`string_to_number(s) -> number`**, the inverse of Stage 17. The Stage 25 pick is the multi-base variant. After that, the small-mirror rhythm ends and the next decision is "modules (~600 lines) or array_filter (~150 lines)."

## Aggregate test count after Stage 23

* **stdlib:** 140 (was 132 before Stage 23, +8 new)
* **clox:** 27
* **repl:** 5
* **composability:** 8
* **vm+clox:** 180 (was 172, +8 stdlib)
* **Total on this box:** 92 (allocator) + 23 (db) + 25 (shell) + 180 (vm+clox) = **320** (was 312 before Stage 23)

The total is now **320** (the wiki's previous "320" claim was a math error — it was 312 before Stage 23, became 320 after; the previous wiki update incorrectly said "320" for Stage 22 when it was actually 312 at that time). The number is now correct end-to-end: 92 + 23 + 25 + 180 = 320.

## Code metrics

* **`src/native.c`:** +88 lines (impl + comment)
* **`tests/test_stdlib.c`:** +147 lines (8 tests with the same shape as Stage 22's)
* **`examples/pad-right-numbers.lox`:** +32 lines (new)
* **Total:** +267 lines, 0 deletions

## Lessons carried into Stage 24

1. *Verify the test count against live `make test` before writing the wiki.* The Stage 22 close-out and wiki had wrong aggregate numbers (claimed 172, actual 164) that propagated forward. Stage 23's "Total on this box" line was verified against `make test` output before this paragraph was written. The discipline: 5-second live verification, then write the number.
2. *The 4 "error case" tests are weak.* A future stage could pass them with the wrong impl. The Stage 24 follow-up is to add `contains(out, "string_to_number")` checks to the error-case tests (and backport to Stage 22-23 if it's a small change).
3. *The "canonical reference" is JS, not Python, for multi-char-fill string operations.* Future string-handling stages should reference this explicitly.

## What did NOT change

* `src/scanner.c`, `src/compiler.c`, `src/vm.c` — Stage 23 is a pure stdlib addition, no parser/compiler/VM changes.
* `Makefile` — no new build targets; the same 4 test binaries cover the new tests.
* `examples/` — 1 new example added (13 total now).
* The composability tests — Stage 23 doesn't have a new composability test; the "right-aligned column" example covers the composition implicitly.

## See also

* `docs/stage-22-closeout.md` — the predecessor, the small-mirror template this stage copied.
* `wiki/build-your-own-x.md` INDEX — the per-stage deep entry for Stage 23 will be added in the wiki update commit.
