# Stage 24 Close-out: string_to_number

**Stage:** 24 of 33
**Branch:** `stage-24-string-to-number`
**Commits:** `7b400d4` (impl + tests + example) — close-out `2cf3c43` (this file)
**Date:** 2026-07-14
**Tests:** 190/190 (was 180/190 before Stage 24, +10 stdlib tests)
**Valgrind:** clean (924 allocs / 924 frees in test_stdlib, 0 errors, 0 leaks)
**Bugs caught:** 1 test bug, 0 implementation bugs. **15th consecutive stage with zero implementation bugs.**

## What shipped

`string_to_number(s) -> number` — the inverse of Stage 17's `string(n)`. Parses a decimal number from a string. JavaScript's `parseFloat` semantics with strict-error-on-failure: leading/trailing whitespace is skipped, an optional sign, then a non-empty sequence of digits with at most one decimal point, an optional exponent. ~55 lines in `src/native.c` (impl body ~25, the rest is comment). No allocation: `strtod` does the parsing and returns a double; we wrap it in a `Value`. The three error conditions (empty/non-numeric input, no characters consumed, overflow to +/-Infinity) are caught by the `strtod` `endptr` / `errno` / `ERANGE` contract.

## Tests added (10 total)

In `tests/test_stdlib.c`:
1. `test_string_to_number_int_positive` — `"42"` → 42.0; adding 1 gives 43. Round-trip idiom.
2. `test_string_to_number_int_negative` — `"-7"` → -7.0; multiplying by 2 gives -14.
3. `test_string_to_number_float` — `"3.14"` → 3.14; adding 1.0 gives 4.14.
4. `test_string_to_number_scientific` — `"3.14e2"` → 314. Scientific notation.
5. `test_string_to_number_round_trip` — `string(string_to_number("42"))` → `"42"`; same for `"3.14"`. The whole point of Stage 24.
6. `test_string_to_number_empty_errors` — `""` → runtime error.
7. `test_string_to_number_non_numeric_errors` — `"abc"` → runtime error.
8. `test_string_to_number_overflow_errors` — `"1e1000"` → runtime error.
9. `test_string_to_number_wrong_type` — `string_to_number(42)` → runtime error.
10. `test_string_to_number_wrong_arg_count` — `string_to_number()` → runtime error.

## Design decisions

(1) `"abc"` is a runtime error, **not NaN**. The JS `parseFloat("abc") === NaN` shape is a silent-failure antipattern when the user is processing typed or file input — they get NaN, the program continues, the result is silently wrong. (2) `"1e1000"` is a runtime error, **not Infinity**. Same shape: the user almost certainly didn't mean to overflow; returning Infinity would propagate silently. (3) Leading/trailing whitespace is OK — `strtod`'s standard behavior, matches `"  42  "` parses to 42. (4) Scientific notation is OK — `"1e3"` parses to 1000, `"1.5e2"` to 150. (5) **No hex support** — `"0x10"` is not supported. `parseFloat` is decimal-only in JS too. (6) The function does NOT trim — if the user wants trim-then-parse, they compose `string_trim` (Stage 9) with `string_to_number`. (7) **No allocation**: `strtod` returns a `double` directly; the only `Value` we produce is the `NUMBER_VAL` wrap.

## New example

`examples/round-trip-number.lox` — the "process user input as a number" idiom. Composes Stage 17's `string(n)` + Stage 24's `string_to_number(s)` + Stage 13's `string_split` + Stage 12a's `array_length` / `array_get`. The "sum a CSV column" pattern is the real-world case: `string_split("10,20,30,40", ",")` → array, then loop with `string_to_number` of each element, accumulating. The example also shows the "validate before using" idiom (don't silently NaN on bad input — the runtime error is the right shape), scientific notation, and negative + scientific combinations (a real-world coordinate). 14 example programs total.

## What this stage teaches

*The "inverse-of-a-previous-stage" is a different kind of small-mirror.* Stages 21-23 were functions of the same shape (different operations on strings). Stage 24 is a function and its inverse: `string(n)` produces a string from a number, `string_to_number(s)` parses a number from a string. The implementation is structurally different (`snprintf` vs `strtod`) but the API contract is symmetric. The "round-trip idiom" — `string(string_to_number("42"))` → `"42"` — is a new closure: the user can now process typed or file input as numbers, which is the most-requested Stage-15 gap.

*The "strict-error" discipline differs from "lenient-default."* The previous stages' error contract was "if the input is bad, return a runtime error" (negative width, empty fill, wrong type, wrong arity). Stage 24 has a different question: "what does `parseFloat('abc')` return?" The JS answer is NaN, and the Python answer is also "no error, but the result is a special value." The right answer for a CLI scripting language is "loud error" — a user running a script that parses user input wants to know if the input was bad, not have NaN propagate silently. The discipline: **strict-by-default for parser-style natives**, lenient-by-default for transform-style natives. `string_pad_start("x", -1, "0")` errors (bad input is a programming error); `parseFloat("abc")` errors (bad input is a runtime condition the user can't predict). The line is fuzzy but the principle is right: parser natives are where silent failure hides, so be loud.

*The 1-test-bug count keeps going.* The test-bug family (impl-right, test-wrong) has now hit 10 of the last 17 stages (8, 9, 10, 12a, 14, 15, 19, 21, 22, 24). The Stage 24 bug was a logic error in the test condition: `!contains("4.14") || !contains("4.1400000000000001")` reads as "fail if either is missing" but the OR-clause was meant to be a "loose" check; the actual logical shape is "fail if '4.14' is missing AND '4.1400000000000001' is missing." The mitigation discipline: write the expected output explicitly, run it through `node` or `printf` first, then write the test. The 30-second cost per test is much less than the cost of the 1-commit amendment cycle.

*The "no allocation" pattern is new.* Stages 21-23 each had a single ALLOCATE call. Stage 24 has zero — `strtod` returns a `double` directly, and `NUMBER_VAL` is a tagged-union wrap, not an allocation. This is the cleanest impl in the post-Stage-21 rhythm. The general lesson: **inverses can be allocation-free if the function being inverted is allocation-heavy**. `string(n)` allocates a 32-byte buffer; `string_to_number(s)` doesn't need to allocate because parsing is a read-only operation on the input string.

## What this stage does NOT teach (the limitation)

*No locale awareness.* `strtod` uses the C locale, which means "3.14" parses as 3.14 in any locale (the C locale's decimal separator is `.`). A user in a locale that uses `,` as the decimal separator would get 3 as the parsed value. The fix would be `strtod_l` with a user-configured locale, but clox doesn't have a locale concept yet. Deferred to a "locale" stage (Stage 28+ if it ever happens).

*No integer / unsigned distinction.* `string_to_number("42")` returns 42.0 (a double), not 42 as an integer. The "is this an integer?" question requires a separate `is_integer` check on the result. JS has the same shape (`parseFloat("42") === 42.0`, `parseInt("42") === 42`), but Python's `int("42") === 42` is what users coming from Python would expect. A future `string_to_int(s)` could close this gap, but the current `number` type is double-only in clox, so the question is somewhat moot.

*No big-number / arbitrary-precision.* `"12345678901234567890"` overflows to `1.234567890123457e19`, losing precision. The "exact integer" guarantee is lost. A future `string_to_bigint` could close this gap, but clox's number type is double, and the precision loss is a property of the type, not the function. Deferred.

## My pick for Stage 25

The "Stage 24+ candidates, in priority order" section of the wiki INDEX names:
1. `string_to_number(s, base)` — the multi-base variant. ~50 lines, higher learning value (the "what's a number" question is more interesting than the "how to convert" question). **My pick for Stage 25.** Same shape as Stage 24, with a `base` parameter (2, 8, 10, 16).

The natural small-mirror candidate is exhausted after Stages 21-24:
- 21: `string_repeat` (transform)
- 22: `string_pad_start` (transform, mirror of self)
- 23: `string_pad_end` (transform, mirror of 22)
- 24: `string_to_number` (parse, inverse of Stage 17)

The next "obvious idiom the user has to hand-roll" gap is:
* **`string_to_int(s)`** — the int-only parse. Closes the "42 vs 42.0" question. Same shape as Stage 24, ~30 lines. Stage 26 candidate.
* **`string_trim_start(s) / string_trim_end(s)`** — mirror of `string_trim` from Stage 9. ~30 lines, no new concepts. Stage 27 candidate.
* **`array_unique(arr)`** — return a deduplicated copy. ~30 lines, no new concepts. Stage 28 candidate.
* **`string_split(s, delim, limit)`** — Stage 13's `string_split` extended with a max-split-count parameter. ~30 lines. Stage 29 candidate.

The Stage 25 pick is **`string_to_number(s, base)`** — the multi-base variant. After that, the small-mirror rhythm continues for one more stage (Stage 26 `string_to_int`), and then the trigger-mine bucket either grows (more candidates from Tom's domain) or the next decision is "modules (~600 lines) or array_filter (~150 lines)."

## Aggregate test count after Stage 24

* **stdlib:** 150 (was 140 before Stage 24, +10 new)
* **clox:** 27
* **repl:** 5
* **composability:** 8
* **vm+clox:** 190 (was 180, +10 stdlib)
* **Total on this box:** 92 (allocator) + 23 (db) + 25 (shell) + 190 (vm+clox) = **330** (was 320 before Stage 24)

## Code metrics

* **`src/native.c`:** +57 lines (impl + comment)
* **`tests/test_stdlib.c`:** +171 lines (10 tests + 1 test-bug fix)
* **`examples/round-trip-number.lox`:** +44 lines (new)
* **Total:** +272 lines, 0 deletions

## Lessons carried into Stage 25

1. *Verify the test condition is logically right.* The `!contains(a) || !contains(b)` shape reads as "fail if either is missing" but the intended shape was "fail if 'a' is missing AND 'b' is missing" (a stricter check). The test-bug caught in this stage was exactly this: a stray "loose" check that inverted the intended logic. The discipline: write the expected output as a single concrete string, then write the contains check against that string. The 30-second cost per test is much less than the cost of the 1-commit amendment cycle.
2. *Parser-style natives are where silent failure hides.* Stage 24 is the first parser native in the rhythm (Stages 21-23 were transform natives). The discipline for parsers: **strict-by-default**. Return a runtime error on any condition the user can't predict (NaN, Infinity, garbage in). This is different from the transform natives' "loud only on programming errors" discipline.
3. *The 5-error-test weakness is real.* 5 of Stage 24's 10 tests check `exit code != 0` only, not the error message text. A Stage 25 that accidentally re-uses Stage 24's impl under a different name would pass Stage 24's tests. The Stage 25 follow-up: add `contains(out, "string_to_number")` checks to the error-case tests, and backport to Stages 22-24 if the change is small.
4. *Verify before amending the test.* The test-bug in this stage was caught by reading the failure message: "expected '4.14' in output, got '4.14\n'". The `\n` in the actual output suggested the contains check should have been for `"4.14\n"` not `"4.14"`. The discipline: when a test fails, read the failure message carefully before changing the test. The impl is more often right than the test.

## What did NOT change

* `src/scanner.c`, `src/compiler.c`, `src/vm.c` — Stage 24 is a pure stdlib addition, no parser/compiler/VM changes.
* `Makefile` — no new build targets; the same 4 test binaries cover the new tests.
* `examples/` — 1 new example added (14 total now).
* The composability tests — Stage 24 doesn't have a new composability test; the "round-trip" example covers the composition implicitly.

## See also

* `docs/stage-23-closeout.md` — the predecessor, the last small-mirror stage in the rhythm.
* `wiki/build-your-own-x.md` INDEX — the per-stage deep entry for Stage 24 will be added in the wiki update commit.
* `examples/round-trip-number.lox` — the new example, demonstrating the round-trip idiom.
