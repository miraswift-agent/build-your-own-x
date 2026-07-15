# Stage 27 Close-out: string_trim_start / string_trim_end

**Stage:** 27 of 33
**Branch:** `stage-27-string-trim-start-end`
**Commits:** `393f777` (impl + tests + example) — close-out `2cf3c43` (this file)
**Date:** 2026-07-14
**Tests:** 221/221 (was 208/221 before Stage 27, +11 stdlib tests)
**Valgrind:** clean (1110 allocs / 1110 frees in test_stdlib, 0 errors, 0 leaks)
**Bugs caught:** 0 implementation bugs, 0 test bugs, 0 example bugs. **2nd consecutive zero-bug stage (after Stage 26's reset; new streak starting at 2).**

## What shipped

`string_trim_start(s) -> string` and `string_trim_end(s) -> string` — two new natives, mirror of Stage 9's `string_trim`. `trimStart` removes leading whitespace only (leaves trailing intact); `trimEnd` removes trailing whitespace only (leaves leading intact). JS reference: `String.prototype.trimStart` / `trimEnd` (also exposed as `trimLeft` / `trimRight` in older specs). The implementation is the same shape as `stringTrimNative` but with only one of the two while-loops. `isWhitespace()` handles spaces, tabs, and newlines (the same set `stringTrimNative` uses). All-whitespace input returns a fresh empty string (the same all-ws case `stringTrimNative` handles). No allocation beyond the `copyString` call (single allocation per call).

## Tests added (11 total)

In `tests/test_stdlib.c`:
1. `test_string_trim_start_basic` — `"   hello   "` → `"hello   "`; `"   hello"` → `"hello"`; `"hello   "` → `"hello   "`. The "leading only" case.
2. `test_string_trim_start_all_whitespace` — `"     "` → `""`. The all-ws case.
3. `test_string_trim_start_tabs_and_newlines` — `"\t\n  hello"` → `"hello"`. Tabs and newlines stripped.
4. `test_string_trim_start_wrong_type` — `string_trim_start(42)` errors.
5. `test_string_trim_start_wrong_arg_count` — 0 args errors; 2 args errors. (2 asserts)
6. `test_string_trim_end_basic` — `"   hello   "` → `"   hello"`; `"hello   "` → `"hello"`. The "trailing only" case.
7. `test_string_trim_end_all_whitespace` — `"     "` → `""`. The all-ws case.
8. `test_string_trim_end_tabs_and_newlines` — `"hello\t\n  "` → `"hello"`. Tabs and newlines stripped.
9. `test_string_trim_end_wrong_type` — `string_trim_end(42)` errors.
10. `test_string_trim_end_wrong_arg_count` — 0 args errors; 2 args errors. (2 asserts)
11. `test_string_trim_compose` — composes `string_trim(string_trim_start(string_trim_end(s)))` and `string_trim_start(s) + "|" + string_trim_end(s)`. The "explicit two-step" idiom.

## Design decisions

(1) **Two new natives, not extensions of `string_trim`.** `trimStart` and `trimEnd` are conceptually distinct from `trim` (one-sided vs two-sided). Same shape as Stage 9's `stringTrimNative` but with only one while-loop. (2) **All-whitespace input returns `""`.** Same as Stage 9. (3) **No allocation beyond the `copyString` call.** Single allocation per call. (4) **`isWhitespace()` handles spaces, tabs, newlines.** The same set as Stage 9. (5) **Strict on input type and arity**, lenient on the input string content (any string, including all whitespace, returns `""` or a partial-trimmed string).

## New example

`examples/trim-one-side.lox` — the "preserve one side" idiom. Composes Stage 9's `string_trim` with Stage 27's `string_trim_start` and `string_trim_end`. Real-world cases:
- **CSV column parsing**: leading whitespace from copy-paste, no trailing whitespace. `trim_start` is the right tool; `trim` would strip a trailing newline that may or may not be present.
- **Keep trailing newline**: log line processing. Strip leading whitespace (indentation) but keep the trailing newline intact.
- **Explicit two-step normalization**: when you want to document the two-step normalization in code (rare, but useful for clarity in pipelines).

17 example programs total.

## What this stage teaches

*The "mirror of an earlier stage" is the cheapest possible small-mirror.* Stages 22-23 were mirrors of Stage 21 (`string_repeat` with same shape, different operation). Stage 27 is a mirror of Stage 9 (`string_trim` with same shape, one side vs both). The implementation is a 10-line subset of the original; the tests follow the same shape; the example composes the two. The discipline: **when a function has a "both sides" variant, the "one side" variants are the next obvious idiom the user has to hand-roll.** `trim` was a gap because the user had to write their own `trimStart` loop.

*The "compose explicit two-step" idiom is sometimes better than the "one-call" idiom.* `string_trim(s)` is the right tool when you want to strip both sides. `string_trim_start(string_trim_end(s))` is the right tool when you want to document the two-step normalization in code (rare, but useful for clarity in pipelines). The discipline: **explicit composition is sometimes better than implicit single-call, when the pipeline is the message.**

*The 2-streak is fragile.* Stage 27 was 2nd-consecutive-zero-bug. The Stage 26 bug was a real impl bug (strtol partial-consumption). The next impl bug is one stage away. The discipline going forward: write a "partial consumption" test in the test-first step for parser natives, before writing the impl. The 30-second cost per parser native is much less than the cost of a 1-impl-bug amendment.

## What this stage does NOT teach (the limitation)

*No `isspace` / `isblank` distinction.* `isWhitespace` in clox's native.c handles three whitespace characters: space, tab, newline. The C standard's `isspace` handles more (form feed `\f`, carriage return `\r`, vertical tab `\v`). The `isWhitespace` in clox is a tighter set. A future "match C isspace" stage could close the gap, but the current behavior is consistent with the user's intuition: trim "the kind of whitespace you see in normal text."

*No Unicode whitespace support.* `isWhitespace` checks ASCII bytes only. A string with non-ASCII whitespace characters (e.g. U+00A0 non-breaking space, U+200B zero-width space) would not be trimmed. Clox is single-byte-string only, so this is a property of the type, not the function. Deferred to a "Unicode strings" stage (probably never, given clox's C-string heritage).

## My pick for Stage 28

The "Stage 27+ candidates, in priority order" section of the wiki INDEX names:
1. `string_trim_start / string_trim_end` (shipped in Stage 27)
2. `array_unique(arr)` — return a deduplicated copy. ~30 lines, no new concepts. **My pick for Stage 28.** The natural small-mirror has been on for 8 stages (21, 22, 23, 24, 25, 26, 27); after Stage 28 the rhythm is genuinely exhausted. The shape: a new native that returns a new array with duplicates removed, preserving order. The "obvious idiom the user has to hand-roll" gap: CSV column dedup, log line dedup, finding the unique values in a list.
3. `string_split(s, delim, limit)` — Stage 13's `string_split` extended with a max-split-count parameter. ~30 lines. Stage 29 candidate.
4. `array_filter(arr, predicate)` — needs user-code dispatch from a native. Architecture work first; this is a "stdlib-in-lox" stage. ~150 lines. **Big swing, not for now.**

The Stage 28 pick is **`array_unique(arr)`**, a new native that returns a deduplicated copy of the input array (preserving first-occurrence order). After that, the small-mirror rhythm has been on for 8 stages; the next decision is "modules (~600 lines) or array_filter (~150 lines) or grow the trigger-mine bucket beyond byox."

## Aggregate test count after Stage 27

* **stdlib:** 181 (was 168 before Stage 27, +11 stdlib tests; +11 added, 0 obsolete)
* **clox:** 27
* **repl:** 5
* **composability:** 8
* **vm+clox:** 221 (was 208, +11 stdlib)
* **Total on this box:** 92 (allocator) + 23 (db) + 25 (shell) + 221 (vm+clox) = **361** (was 348 before Stage 27)

## Code metrics

* **`src/native.c`:** +69 lines (impl body ~30, comment ~20, globals registration ~19)
* **`tests/test_stdlib.c`:** +204 lines (11 tests + boilerplate)
* **`examples/trim-one-side.lox`:** +68 lines (new)
* **Total:** +341 lines, 0 deletions

## Lessons carried into Stage 28

1. *Mirror of an earlier stage is the cheapest possible small-mirror.* When a function has a "both sides" variant, the "one side" variants are the next obvious idiom. The discipline: look at existing functions for the "one side" gap.
2. *Explicit two-step composition is sometimes better than single-call.* `string_trim_start(string_trim_end(s))` documents the pipeline; `string_trim(s)` does the work. The right tool depends on whether the pipeline is the message.
3. *The 2-streak is fragile.* Stage 26 broke the 16-streak. The next impl bug is one stage away. The discipline going forward: write a "partial consumption" test in the test-first step for parser natives, before writing the impl.
4. *isWhitespace's scope is intentional.* clox's `isWhitespace` handles space, tab, newline — not the full C `isspace` set, not Unicode. The current behavior is consistent with the user's intuition: trim "the kind of whitespace you see in normal text." A future stage could expand the set, but the current behavior is right for clox's single-byte-string model.

## What did NOT change

* `src/scanner.c`, `src/compiler.c`, `src/vm.c` — Stage 27 is a pure stdlib addition, no parser/compiler/VM changes.
* `Makefile` — no new build targets; the same 4 test binaries cover the new tests.
* `examples/` — 1 new example added (17 total now).
* The composability tests — Stage 27 doesn't have a new composability test; the "trim-one-side" example covers the composition implicitly.

## See also

* `docs/stage-9-closeout.md` — the predecessor `string_trim` (Stage 9), which Stage 27 mirrors.
* `wiki/build-your-own-x.md` INDEX — the per-stage deep entry for Stage 27 will be added in the wiki update commit.
* `examples/trim-one-side.lox` — the new example, demonstrating the one-side trim idiom.
