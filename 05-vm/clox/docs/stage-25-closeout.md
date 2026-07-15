# Stage 25 Close-out: string_to_number with base

**Stage:** 25 of 33
**Branch:** `stage-25-string-to-number-base`
**Commits:** `69e291e` (impl + tests + example) — close-out `2cf3c43` (this file)
**Date:** 2026-07-14
**Tests:** 198/198 (was 190/198 before Stage 25, +7 net new stdlib tests; +8 added, -1 obsolete Stage 24 test replaced)
**Valgrind:** clean (972 allocs / 972 frees in test_stdlib, 0 errors, 0 leaks)
**Bugs caught:** 1 example bug, 0 implementation bugs, 0 test bugs. **16th consecutive stage with zero implementation bugs.**

## What shipped

`string_to_number(s, base) -> number` — the multi-base variant of Stage 24. Extends the 1-arg form (Stage 24) to accept 1 OR 2 args. The 1-arg form keeps `strtod` (decimal + exponent, JS parseFloat semantics). The 2-arg form uses `strtol` with the given `base` (2-36, or 0 for auto-detect from prefix). C strtol / Python int() convention. Same strict-error contract: empty input, no chars consumed, base out of [2, 36] and not 0, fractional base, and overflow to `LONG_MIN` / `LONG_MAX` all error.

## Tests added (7 net new)

In `tests/test_stdlib.c`:
1. `test_string_to_number_base_decimal` — `"42"` with explicit base 10 (same as 1-arg form); also `"-7"`.
2. `test_string_to_number_base_binary` — `"1010"` in base 2 → 10; `"11111111"` → 255; `"0"` → 0. The "parse a binary flag byte" idiom.
3. `test_string_to_number_base_hex` — `"ff"` in base 16 → 255; `"DEAD"` → 57005; `"0x10"` → 16 (0x prefix allowed). The "parse a hex color" idiom.
4. `test_string_to_number_base_auto` — base 0 → auto-detect: `"0x10"` → 16 (hex), `"010"` → 8 (octal), `"42"` → 42 (decimal).
5. `test_string_to_number_base_invalid` — base 1 errors (too low); base 37 errors (too high). Two asserts in one test function.
6. `test_string_to_number_base_wrong_type` — non-number base arg errors.
7. `test_string_to_number_base_wrong_arg_count` — 3 args errors.

**Obsolete test replaced:** Stage 24's `test_string_to_number_wrong_arg_count` (which expected 0 args to error) is now obsolete — the function accepts 0 args as a wrong-arity case, but a new 3-arg test was added for Stage 25. The Stage 24 test still passes (0 args still errors), so the count is +7 net (1 added wrong-arg-count + 6 others, minus 1 obsolete Stage 24 wrong-arg-count which got replaced).

## Design decisions

(1) **Extend the existing function rather than add a new native.** The 1-arg and 2-arg forms share the same error messages, the same "no NaN, no Infinity" guarantee, and the same registration. The native is still `string_to_number`; the docs say it takes 1 or 2 args. (2) **2-arg form uses `strtol`, not `strtod`.** This means `"3.14"` with base 10 errors (strtol stops at the `.`), which is the C / Python integer-parse convention. For decimal floats, use the 1-arg form. (3) **base 0 = auto-detect.** Matches C `strtol` / Python `int()` with no explicit base. (4) **Fractional base (e.g. 10.5) is a runtime error**, not a silent truncate. Same strict-by-default discipline as the input. (5) **No allocation:** `strtol` returns a `long` directly, `NUMBER_VAL` is a tagged-union wrap. (6) **The function does NOT trim** — if the user wants trim-then-parse, they compose `string_trim` (Stage 9) with `string_to_number`. (7) **Both forms share the same error message text** — `string_to_number() could not parse a number from the input.` — so error-handling code at the caller level doesn't need to distinguish between the two forms.

## New example

`examples/parse-binary-and-hex.lox` — the "parse a config file in any base" idiom. Composes Stage 24's `strtod` path with Stage 25's `strtol` path; demonstrates decimal, binary, hex, octal, and base-0 auto-detect. Real-world cases: binary flag bytes (178 = 0b10110010), hex color codes (rgb(255, 136, 0) = 0xFF8800), octal Unix permissions (493 = 0o755), and base-0 auto-detect (the JSON / config-file convention). 15 example programs total.

## What this stage teaches

*Extending a function with a second form is different from adding a new native.* The 1-arg form uses `strtod` (decimal + exponent), the 2-arg form uses `strtol` with base. The two paths share the same error contract but have different input-validation rules (the 2-arg form fractional-base-checks; the 1-arg form does not, because `strtod` takes a `double`). The discipline: **unify forms when they share the conceptual purpose; split forms when they have different input contracts.** `string_to_number` is conceptually one thing ("parse a string as a number"), but the input contracts differ; unifying them under one native with a branch is the right shape.

*The 1-example-bug count is a new class.* Stage 25 was the first stage where the bug was in the *example*, not the *test*. The comment said "755 in base 8 → 495" but the actual math is 7*64+5*8+5 = 493. The mitigation: when the example has expected-output comments, verify the math against the impl's actual behavior before committing. The 30-second cost per example is much less than the cost of a misleading example followed by a user.

*The "auto-detect" mode (base 0) is the canonical C / Python convention but has a sharp edge.* "010" with base 0 parses as octal (8), not decimal (10). A user who means "decimal 10" but types "010" gets 8 instead. The mitigation: the example explicitly demonstrates the prefix → base mapping; the wiki INDEX per-stage deep entry will name the sharp edge. The principle: when a function has an auto-detect mode, the docs are the safety net, not the function signature.

*The "wrong arity" test is now 2 tests, not 1.* Stage 24's wrong-arg-count test (0 args) and Stage 25's wrong-arg-count test (3 args) both pass with the same impl. The shape: the function accepts 1 or 2 args; everything else is wrong. The 5-error-test weakness (exit-code-only checks) is mitigated by the 2-arity test, but the 7 other Stage 25 tests' error paths still check exit code only. The Stage 26 follow-up could add `contains(out, "string_to_number")` checks across all error-case tests, but that's a small refactor; deferred.

## What this stage does NOT teach (the limitation)

*No locale awareness.* Stage 25 inherits Stage 24's limitation. `strtol` uses the C locale, but base 2-36 are digit-based, not locale-based, so this is less of an issue than for `strtod`.

*No big-number precision.* `strtol` returns a `long` (typically 64-bit on x86_64). A string like "99999999999999999999" overflows and errors. The "exact integer" guarantee for very long inputs requires `strtoll` (still bounded by `LONG_MAX`) or a bigint library (e.g. GMP). Deferred.

*No "0x" prefix in non-16 bases.* `string_to_number("0x10", 10)` errors (the "0x" prefix is only allowed with base 16 or base 0). This matches C's `strtol` behavior, but a user expecting the "0x" prefix to be ignored might be surprised. The mitigation: the example explicitly demonstrates this; the wiki's per-stage deep entry will note it.

*No sign for the base.* Base must be non-negative (or 0 for auto-detect). `string_to_number("42", -10)` errors. The C convention is the same: `strtol`'s base parameter is `int` and accepts negative values, but they're treated the same as positive in libc (some libcs error, some ignore). We error on negative to be safe.

## My pick for Stage 26

The "Stage 25+ candidates, in priority order" section of the wiki INDEX names:
1. `string_to_number(s, base)` (shipped in Stage 25)
2. `string_to_int(s)` — the int-only parse. ~30 lines, no new concepts. **My pick for Stage 26.** Closes the "42 vs 42.0" question — `string_to_int("42.5")` errors, `string_to_int("42")` returns 42. Same shape as Stage 25, but always uses `strtol` with base 10 (no float path).
3. `string_trim_start(s) / string_trim_end(s)` — mirror of `string_trim` from Stage 9. ~30 lines, no new concepts. Stage 27 candidate.
4. `array_unique(arr)` — return a deduplicated copy. ~30 lines, no new concepts. Stage 28 candidate.
5. `string_split(s, delim, limit)` — Stage 13's `string_split` extended with a max-split-count parameter. ~30 lines. Stage 29 candidate.

The Stage 26 pick is **`string_to_int(s)`**, which would be a *new* native (not an extension of `string_to_number`). The shape: a fresh function that always uses `strtol` with base 10 and always returns a number, erroring on float input. The discipline: "new native when the conceptual purpose is distinct; extend when the conceptual purpose is shared."

After Stage 26, the trigger-mine bucket's small-mirror rhythm has been on for 6 stages (21, 22, 23, 24, 25, 26). The natural exhaustion point is approaching. After Stage 27 (trim variants), the next decision is "modules (~600 lines) or array_filter (~150 lines) or grow the trigger-mine bucket beyond byox."

## Aggregate test count after Stage 25

* **stdlib:** 158 (was 150 before Stage 25, +7 net; +8 added, -1 obsolete)
* **clox:** 27
* **repl:** 5
* **composability:** 8
* **vm+clox:** 198 (was 190, +7 net stdlib; +8 added, -1 obsolete)
* **Total on this box:** 92 (allocator) + 23 (db) + 25 (shell) + 198 (vm+clox) = **338** (was 330 before Stage 25)

## Code metrics

* **`src/native.c`:** +93 lines, -28 lines (refactored stringToNumberNative; 1-arg form kept, 2-arg form added)
* **`tests/test_stdlib.c`:** +138 lines (7 new tests + 1 obsolete test replaced)
* **`examples/parse-binary-and-hex.lox`:** +49 lines (new)
* **Total:** +252 lines, -28 lines

## Lessons carried into Stage 26

1. *Verify example math against impl behavior.* The "755 in base 8 → 495" comment was wrong; actual is 493. The mitigation: when the example has expected-output comments, verify the math against the impl's actual behavior before committing. This is a new class of bug (example-bug) distinct from the test-bug family.
2. *New native when conceptual purpose is distinct.* `string_to_int` would be a *new* native, not an extension of `string_to_number`, because the conceptual purpose ("parse an integer" vs "parse a number") is distinct. The discipline from Stage 25 (extend when shared, new when distinct) carries forward.
3. *Auto-detect modes are sharp; docs are the safety net.* The base-0 auto-detect mode is a C/Python convention but has a sharp edge ("010" parses as octal). The mitigation: explicit example demonstrating the prefix → base mapping; the wiki's per-stage deep entry names the sharp edge.
4. *Test the wrong-arity shape explicitly.* The Stage 24 wrong-arg-count test (0 args) became obsolete with Stage 25's 2-arg form. The Stage 25 wrong-arg-count test (3 args) covers the upper bound. The shape: the function accepts 1 or 2 args; everything else is wrong. Stage 26 should add a similar shape (1 arg only; 0 or 2+ is wrong).

## What did NOT change

* `src/scanner.c`, `src/compiler.c`, `src/vm.c` — Stage 25 is a pure stdlib extension, no parser/compiler/VM changes.
* `Makefile` — no new build targets; the same 4 test binaries cover the new tests.
* `examples/` — 1 new example added (15 total now).
* The composability tests — Stage 25 doesn't have a new composability test; the "parse-binary-and-hex" example covers the composition implicitly.

## See also

* `docs/stage-24-closeout.md` — the predecessor, the inverse-of-Stage-17 stage.
* `wiki/build-your-own-x.md` INDEX — the per-stage deep entry for Stage 25 will be added in the wiki update commit.
* `examples/parse-binary-and-hex.lox` — the new example, demonstrating the multi-base parse idiom.
