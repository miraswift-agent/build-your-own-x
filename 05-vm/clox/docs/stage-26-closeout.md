# Stage 26 Close-out: string_to_int

**Stage:** 26 of 33
**Branch:** `stage-26-string-to-int`
**Commits:** `6ebed37` (impl + tests + example) — close-out `2cf3c43` (this file)
**Date:** 2026-07-14
**Tests:** 208/208 (was 198/208 before Stage 26, +8 stdlib tests)
**Valgrind:** clean (1032 allocs / 1032 frees in test_stdlib, 0 errors, 0 leaks)
**Bugs caught:** 1 implementation bug, 0 test bugs, 0 example bugs. **16-streak broken; reset to 1.** First stage with a real implementation bug since Stage 21.

## What shipped

`string_to_int(s) -> number` — a NEW native (not an extension of `string_to_number`). Closes the "42 vs 42.0" question: `string_to_int("42.5")` errors (strtol stops at the `.`, and we treat mid-string stop as "no integer parsed"), `string_to_int("42")` returns 42.0 (a double, since clox's number type is double — the user's *intent* is integer, the *type* is still double). Same strict-error contract as Stage 24/25: empty input, no chars consumed, partial consumption, and overflow to LONG_MIN / LONG_MAX all error. No NaN, no Infinity.

## The implementation bug (and the postmortem)

The first version checked `endptr == s->chars` (strtol's "no chars consumed" signal) but did NOT check `endptr == s->chars + s->length` (all chars consumed). strtol's default behavior is "consume as much as possible, ignore the rest" — `strtol("42.5")` returns 42 with endptr pointing to ".5". The float-error tests caught this on the first test run. Fix: require `endptr == s->chars + s->length` (full consumption).

**The discipline this violated**: "impl right from the first test run." The streak was 16 consecutive zero-impl-bug stages (Stages 10-25). Stage 26 breaks the streak because the impl was wrong on the first run and right on the second. The 1-impl-bug count now goes from "1 in 16 stages" to "2 in 17 stages" — still a 1-in-8 ratio, but the absolute count is non-zero. The reset to 1 is honest: a streak is "consecutive zero-bug stages from the first test run," and Stage 26's first test run had a bug.

**The lesson**: strtol's contract has TWO endptr signals:
- (a) `endptr == startptr`: no chars consumed (empty, "abc", "  ")
- (b) `endptr != startptr + length`: partial consumption ("42.5", "42abc", "  42")

Both must be checked for strict-error parser behavior. The discipline: **for parser natives, "strict" means "all chars consumed", not "some chars consumed."** The C convention (silent partial consumption) is wrong for parser natives; the Python convention (loud error on partial consumption) is right.

**The mitigation discipline going forward**: when designing parser natives, write a "partial consumption" test in the test-first step, before writing the impl. The test should explicitly check `string_to_int("42.5")` and `string_to_int("42abc")` and `string_to_int("  42")` (mid-string stop cases). If the test passes when the impl is wrong, the test is wrong. The 30-second cost per parser native is much less than the cost of a 1-impl-bug amendment.

## Tests added (8 total)

In `tests/test_stdlib.c`:
1. `test_string_to_int_positive` — `"42"` → 42; adding 1 gives 43. The basic round-trip.
2. `test_string_to_int_negative` — `"-7"` → -7; multiplying by 2 gives -14. The signed-int case.
3. `test_string_to_int_round_trip` — `string(string_to_int("42"))` → `"42"`; same for "-7" and "0". The whole point of Stage 26.
4. `test_string_to_int_float_errors` — `"42.5"` errors; `"3.14"` errors. The float-strictness test (caught the impl bug). Two asserts in one function.
5. `test_string_to_int_empty_errors` — `""` errors. The empty-input test.
6. `test_string_to_int_non_numeric_errors` — `"abc"` errors. The non-numeric-input test.
7. `test_string_to_int_wrong_type` — `string_to_int(42)` errors. The wrong-type test.
8. `test_string_to_int_wrong_arg_count` — `string_to_int()` errors; `string_to_int("42", 10)` errors. The wrong-arity test (2 asserts).

## Design decisions

(1) **A NEW native, not an extension of `string_to_number`.** Integer-only parse is conceptually distinct from general number parse. (2) **Always base 10, no auto-detect, no base parameter.** If the user wants base 16 parsing, they use `string_to_number(s, 16)` (Stage 25). The two natives have distinct conceptual purposes; the "which one do I use?" question is decided by intent (integer or number), not by input shape. (3) **`"42.5"` errors (full-consumption strictness).** Matches Python's `int("42.5")` error shape, not JavaScript's `parseInt("42.5") === 42` silent-truncate. (4) **No leading whitespace allowed** (strtol skips leading WS by default; we override to strict). (5) **`"+42"` is accepted** (strtol accepts it; we follow the C convention). The "no leading '+'" is a minor difference from a strict hand-rolled integer parser, but it's the C convention; deferred to a "custom parser" stage if it ever happens. (6) **No allocation**: strtol returns a `long` directly; `NUMBER_VAL` is a tagged-union wrap. (7) **The function does NOT trim** — if the user wants trim-then-parse, they compose `string_trim` (Stage 9) with `string_to_int`.

## New example

`examples/parse-integer.lox` — the "parse a CSV column of integers" idiom. Composes Stage 13's `string_split` + Stage 26's `string_to_int` + Stage 12a's `array_length`/`array_get`. The "sum a CSV column of integers" pattern: `string_split("10,20,30,40", ",")` → array, then loop with `string_to_int` of each element, accumulating. 16 example programs total.

## What this stage teaches

*"`strtol` has two endptr signals; both must be checked."* The C convention is "consume as much as possible, ignore the rest." The Python convention is "loud error on partial consumption." For parser natives, the Python convention is right: silent partial consumption is a sharp edge that hides user errors. The discipline: **for parser natives, "strict" means "all chars consumed", not "some chars consumed."** The mitigation: write a "partial consumption" test in the test-first step, before writing the impl.

*"New native when conceptual purpose is distinct" (from Stage 25's discipline) carried forward.* `string_to_int` is not an extension of `string_to_number` because integer-only parse is conceptually distinct from general number parse. The shape: a new native with its own registration, its own test set, its own close-out doc, its own example. The discipline from Stage 25 ("unify when conceptual purpose is shared; new when distinct") is the right answer for this kind of decision.

*The 16-streak is broken; the reset to 1 is honest.* A streak is "consecutive zero-bug stages from the first test run," and Stage 26's first test run had a real impl bug. The discipline is not "ratchet the count up" but "count the real outcomes." A reset to 1 is the right call: the next stage that ships with zero impl bugs on the first run is the start of a new streak.

*The "5-error-test weakness" is now 7-error-test weakness.* Stage 26 has 8 tests; 5 of them are error-case (float, empty, non-numeric, wrong-type, wrong-arg-count) and 3 are happy-path. The 5 error-case tests check exit code only, not error message text. The Stage 27 follow-up could add `contains(out, "string_to_int")` checks across all error-case tests, but that's a small refactor; deferred.

## What this stage does NOT teach (the limitation)

*No big-number precision.* `strtol` returns a `long` (typically 64-bit on x86_64). A string like "99999999999999999999" overflows and errors. The "exact integer" guarantee for very long inputs requires a bigint library (e.g. GMP). Deferred.

*No int/unsigned distinction.* `string_to_int("-7")` returns -7.0 (a double), not -7 as an int. The "is this an int?" question is satisfied by the function's intent; the *type* is still double. This is a clox-number-type question, not a function question. If clox adds an int type, `string_to_int` would return an int; for now, double.

*Leading "+" accepted.* `string_to_int("+42")` returns 42 (strtol accepts it; we follow C). A hand-rolled strict parser would reject this. The "no leading '+'" is a minor difference from a strict integer parser, but it's the C convention.

## My pick for Stage 27

The "Stage 26+ candidates, in priority order" section of the wiki INDEX names:
1. `string_to_int(s)` (shipped in Stage 26)
2. `string_trim_start(s) / string_trim_end(s)` — mirror of `string_trim` from Stage 9. ~30 lines, no new concepts. **My pick for Stage 27.** Two new natives that trim whitespace from the start or end (but not both) of a string. The natural small-mirror candidates are still going strong at Stage 27.
3. `array_unique(arr)` — return a deduplicated copy. ~30 lines, no new concepts. Stage 28 candidate.
4. `string_split(s, delim, limit)` — Stage 13's `string_split` extended with a max-split-count parameter. ~30 lines. Stage 29 candidate.

The Stage 27 pick is **`string_trim_start(s) / string_trim_end(s)`**. After that, the small-mirror rhythm will have been on for 7 stages (21, 22, 23, 24, 25, 26, 27). The natural exhaustion point is approaching. After Stage 28, the next decision is "modules (~600 lines) or array_filter (~150 lines) or grow the trigger-mine bucket beyond byox."

## Aggregate test count after Stage 26

* **stdlib:** 168 (was 158 before Stage 26, +8 stdlib tests)
* **clox:** 27
* **repl:** 5
* **composability:** 8
* **vm+clox:** 208 (was 198, +8 stdlib)
* **Total on this box:** 92 (allocator) + 23 (db) + 25 (shell) + 208 (vm+clox) = **348** (was 338 before Stage 26)

## Code metrics

* **`src/native.c`:** +89 lines (impl + comment + globals registration)
* **`tests/test_stdlib.c`:** +159 lines (8 tests + boilerplate)
* **`examples/parse-integer.lox`:** +48 lines (new)
* **Total:** +296 lines, 0 deletions

## Lessons carried into Stage 27

1. *For parser natives, "strict" means "all chars consumed", not "some chars consumed."* Write a "partial consumption" test in the test-first step, before writing the impl. The 30-second cost per parser native is much less than the cost of a 1-impl-bug amendment.
2. *The streak is honest.* A streak is "consecutive zero-bug stages from the first test run," and Stage 26's first test run had a real impl bug. The reset to 1 is the right call; a streak is not a metric to inflate, it's a discipline to apply.
3. *strtol's two endptr signals.* (a) `endptr == startptr` = no chars consumed. (b) `endptr != startptr + length` = partial consumption. Both must be checked for strict-error parser behavior. The C convention is silent partial consumption; the Python convention is loud error. For parser natives, the Python convention is right.
4. *New native when conceptual purpose is distinct.* `string_to_int` is a new native, not an extension of `string_to_number`. The discipline from Stage 25 ("unify when conceptual purpose is shared; new when distinct") carries forward.

## What did NOT change

* `src/scanner.c`, `src/compiler.c`, `src/vm.c` — Stage 26 is a pure stdlib addition, no parser/compiler/VM changes.
* `Makefile` — no new build targets; the same 4 test binaries cover the new tests.
* `examples/` — 1 new example added (16 total now).
* The composability tests — Stage 26 doesn't have a new composability test; the "parse-integer" example covers the composition implicitly.

## See also

* `docs/stage-25-closeout.md` — the predecessor, the multi-base variant of Stage 24.
* `wiki/build-your-own-x.md` INDEX — the per-stage deep entry for Stage 26 will be added in the wiki update commit.
* `examples/parse-integer.lox` — the new example, demonstrating the integer round-trip idiom.
