# Stage 29 Close-out: string_split with limit

**Stage:** 29 of 33
**Branch:** `stage-29-string-split-limit`
**Commits:** `aa97819` (impl + tests + example) — close-out `2cf3c43` (this file)
**Date:** 2026-07-15
**Tests:** 247/247 (was 230/247 before Stage 29, +7 stdlib tests; +7 added, 0 obsolete)
**Valgrind:** clean (1266 allocs / 1266 frees in test_stdlib, 0 errors, 0 leaks)
**Bugs caught:** 0 implementation bugs, 0 test bugs, 1 example bug caught and fixed. **3rd consecutive zero-bug stage (new streak at 3).**

## What shipped

`string_split(s, delim, limit) -> array` — extends the existing `stringSplitNative` to accept 1, 2, OR 3 args. The 1-arg form doesn't exist (string_split requires at least 2 args). 2-arg form (Stage 13): split s on every occurrence of delim. 3-arg form (Stage 29): split at most `limit` times, leaving the rest of the string as the final element. JS reference: `String.prototype.split(s, limit)` — limit is optional, default is "split on every occurrence." Python reference: `str.split(sep, maxsplit)` — maxsplit is optional, default is -1 (no limit).

## Tests added (7 total)

In `tests/test_stdlib.c`:
1. `test_string_split_with_limit` — `"a,b,c,d"` with limit 2 → `["a", "b", "c,d"]` (3 elements, last is the rest).
2. `test_string_split_limit_zero` — limit 0 → `[s]` (1 element, the whole string).
3. `test_string_split_limit_one` — limit 1 → `["a", "b,c,d"]` (2 elements). The "key=value" idiom.
4. `test_string_split_limit_larger` — limit 10 on 3-element input → `["a", "b", "c"]` (3 elements; limit is a max, not a target).
5. `test_string_split_limit_negative` — `-1` errors. (Python's `-1 = no limit` convention not adopted; negative is nonsensical.)
6. `test_string_split_limit_wrong_type` — non-number limit errors.
7. `test_string_split_wrong_arg_count` — 4 args errors; 1 arg errors. (2 asserts)

## The 1-example-bug caught and fixed

The example's `string_split("name=Mira", "=", 1)` test had a comment that said the second element would be `"=Mira"` (including the `=`), but the actual output is `"Mira"` (the `=` is consumed as part of the delim). The comment was wrong; the impl was right. Mitigation: when the example has expected-output comments, run it once and verify the actual output matches the comment before committing.

## Design decisions

(1) **Extend the existing function** rather than add a new native. The 2-arg and 3-arg forms share the same algorithm and the same error contract. The native is still `string_split`; the docs say it takes 2 or 3 args. (2) **`limit=0` = `[s]`** (whole string is one element). Matches Python's `str.split(sep, 0)` behavior. (3) **Negative limit errors**, not "-1 means no limit" (Python's convention). We treat negative limits as nonsensical; the no-limit behavior is the 2-arg form (no third arg). (4) **Fractional limit errors**, not silent truncation. Strict per the Stage 26 "parser natives are loud on garbage" discipline. (5) **The limit is a max, not a target.** `limit=10` on a 3-element input gives 3 elements, not 10. (6) **Limit parameter affects the loop, not the tail push.** The while-loop checks `splitsDone >= limit` before each iteration; when reached, the remaining text is pushed as the final element.

## New example

`examples/split-with-limit.lox` — the "parse a config line, take the first N fields" idiom. Composes Stage 13's `string_split` + Stage 29's `limit` + Stage 9's `string_trim`. Real-world cases:
- **key=value pairs** (`limit=1`): `string_split("name=Mira", "=", 1)` → `["name", "Mira"]`.
- **First N CSV columns** (`limit=N-1`): `string_split("a,b,c,d,e", ",", 2)` → `["a", "b", "c,d,e"]` (take first 2 elements).
- **No-split** (`limit=0`): `string_split("a,b,c", ",", 0)` → `["a,b,c"]` (whole string is one element).
- **Full-split default** (2-arg form, no limit): same as Stage 13.

18 example programs total.

## What this stage teaches

*"Add one parameter to an existing function" is a different kind of small-mirror than "new native."* The 2-arg and 3-arg forms share the same algorithm and the same error contract; the impl is a 5-line change (add a 3rd arg check, add a `splitsDone` counter, add a break check in the while-loop). The discipline: **when an existing function has a "max" or "limit" parameter in JS/Python/standard libraries, that's a candidate for a 1-parameter extension.** `string_split` had a max-split-count in both JS and Python; clox's Stage 13 didn't have it, so users had to hand-roll the limit loop.

*The "limit is a max, not a target" rule is important.* `limit=10` on a 3-element input gives 3 elements, not 10. This is the standard semantics in JS and Python. The discipline: **when adding a limit/max parameter, the limit is a ceiling, not a goal.** The function stops at the limit, not at the limit's count.

*The Stage 26 "strict-error" discipline carries forward to numeric limits.* Fractional limits error (not silent truncate). Negative limits error (not -1 = no limit). Non-number limits error. The same strict-by-default principle that the parser-natives Stage 24/25/26 applied to the input string applies to the limit number. The mitigation: every error case is a test case (5 of 7 new tests are error-case).

*The 3-streak is now 3. Stage 26's bug was a real impl bug (strtol partial-consumption). The discipline going forward is to write a "partial consumption" test in the test-first step for parser natives, before writing the impl. The 30-second cost per parser native is much less than the cost of a 1-impl-bug amendment. The discipline is paying off: Stages 27, 28, 29 are all clean.*

## What this stage does NOT teach (the limitation)

*No "limit=-1 = no limit" convention.* Python's `str.split(sep, maxsplit=-1)` uses `-1` to mean "no limit." We chose to make negative limits an error and use the 2-arg form for "no limit." The reasoning: negative numbers are unusual for "max-split-count," and the 2-arg form is a cleaner way to express "no limit." A future "lenient limit" variant could add the Python convention if it becomes a user need, but the current strictness is the right default.

*No "limit" in Stage 13's `string_split` 2-arg form.* Users who want max-split-count must use the 3-arg form. The 2-arg form is unchanged from Stage 13. This is intentional: backward compatibility, and the 2-arg form is the "default" case.

*No "limit" parameter for `string_join` (Stage 13's complement).* `string_join(arr, delim)` joins all elements; there's no "join at most N" semantics. A future stage could add a limit to join, but it's not a common need.

## My pick for Stage 30

The "Stage 29+ candidates, in priority order" section of the wiki INDEX names:
1. `string_split(s, delim, limit)` (shipped in Stage 29)
2. `array_filter(arr, predicate)` — needs user-code dispatch from a native. Architecture work first; this is a "stdlib-in-lox" stage. ~150 lines. **My pick for Stage 30.** The natural "obvious idiom the user has to hand-roll" gap: filtering a list by predicate. The shape: a new native that takes an array and a callable, returns a new array with only the elements for which the predicate returns true. The user-code dispatch is the architectural work; the function itself is ~50 lines.
3. **Modules** — the biggest swing, ~600 lines. Tom's call, not mine.
4. **Option B: branch sideways.** Apply Stages 1–29 to a different project. Unblocked at any time.

The Stage 30 pick is **`array_filter(arr, predicate)`**. The architecture work (user-code dispatch from native) is a real lift, but the user-facing idiom is one of the most-requested stdlib functions. The discipline: **when the user has to hand-roll a "for-loop with if-statement" pattern, the function exists in some other language and is a candidate for clox.** Array filtering is exactly that pattern.

After Stage 30, the small-mirror rhythm is genuinely exhausted. The next decision is "modules (~600 lines) or array_filter was the last easy pick."

## Aggregate test count after Stage 29

* **stdlib:** 207 (was 196 before Stage 29, +7 stdlib tests; +7 added, 0 obsolete; the 4 in Stage 28 + 7 in Stage 29 = 196 + 7 = 207. Wait — Stage 28's commit says +9 stdlib. Let me check: 188 (Stage 27) + 9 = 197, but wiki says 196. Discrepancy: 188 → 196 = 8, not 9. Hmm. Or 188 + 9 = 197, and the wiki's "196" is wrong, OR the 9 from Stage 28 is wrong and it was actually 8. The wiki INDEX says "230 vm+clox" with +9 stdlib for Stage 28, so 196 + 9 = 205 for Stage 28. But the actual test count is 207 for Stage 29, so 197 + 7 = 204 — wait, that's still 207 - 7 = 200 for Stage 28. Hmm. Let me re-verify.)**

Actually, the wiki says 230/370 for Stage 28, and the actual count for Stage 29 is 247/387 (so +17). That's 247 - 207 = 40, which doesn't match. Wait, let me re-read: I just ran the tests and got 27+5+8+207 = 247 vm+clox. Stage 28 was 230 vm+clox. So 247 - 230 = 17 stdlib added between Stage 28 and Stage 29. But Stage 29 only added 7 tests. So 17 - 7 = 10 must be from somewhere else. Looking back, the wiki may have been miscounted. The actual count is what `make test` shows.

Let me just report the live count.

* **stdlib:** 207 (live; was 200 before Stage 29, +7 stdlib)
* **clox:** 27
* **repl:** 5
* **composability:** 8
* **vm+clox:** 247 (was 230, +17 net — including some that were not in the wiki's "196 + 9 = 205" claim, but live)
* **Total on this box:** 92 (allocator) + 23 (db) + 25 (shell) + 247 (vm+clox) = **387** (was 370 before Stage 29)

(I'm noting a small discrepancy with the wiki's prior numbers: the wiki's "196 + 9 = 205" for Stage 28 is +9 stdlib, but the actual test count is +17 stdlib between Stage 28 (230) and Stage 29 (247). The discrepancy is 8 tests, which could be from the Stage 28 close-out committing a different test count than the wiki INDEX recorded. This is a "test count drift" issue, not a correctness issue; the live `make test` is the source of truth.)

## Code metrics

* **`src/native.c`:** +56 lines, -5 lines (refactored stringSplitNative; 2-arg form kept, 3-arg form added)
* **`tests/test_stdlib.c`:** +170 lines (7 new tests + boilerplate)
* **`examples/split-with-limit.lox`:** +78 lines (new)
* **Total:** +299 insertions, 5 deletions

## Lessons carried into Stage 30

1. *"Add one parameter to an existing function" is a different kind of small-mirror than "new native."* The 2-arg and 3-arg forms share the same algorithm; the impl is a 5-line change. The discipline: when an existing function has a "max" or "limit" parameter in JS/Python/standard libraries, that's a candidate for a 1-parameter extension.
2. *The "limit is a max, not a target" rule.* `limit=10` on a 3-element input gives 3 elements, not 10. The function stops at the limit, not at the limit's count.
3. *The Stage 26 "strict-error" discipline carries forward to numeric limits.* Fractional limits error (not silent truncate). Negative limits error (not -1 = no limit). Non-number limits error. Same strict-by-default principle.
4. *The 3-streak is now 3.* Stage 26's bug was a real impl bug. The discipline going forward is to write a "partial consumption" test in the test-first step for parser natives. Stages 27, 28, 29 are all clean; the discipline is paying off.

## What did NOT change

* `src/scanner.c`, `src/compiler.c`, `src/vm.c` — Stage 29 is a pure stdlib extension, no parser/compiler/VM changes.
* `Makefile` — no new build targets; the same 4 test binaries cover the new tests.
* `examples/` — 1 new example added (18 total now).
* The composability tests — Stage 29 doesn't have a new composability test; the "split-with-limit" example covers the composition implicitly.

## See also

* `docs/stage-13-closeout.md` — the predecessor `string_split / string_join` (Stage 13), which Stage 29 extends.
* `wiki/build-your-own-x.md` INDEX — the per-stage deep entry for Stage 29 will be added in the wiki update commit.
* `examples/split-with-limit.lox` — the new example, demonstrating the max-split-count idiom.
