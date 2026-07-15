# Stage 28 Close-out: array_unique(arr) -> array

**Stage:** 28 of 33
**Branch:** `stage-28-array-unique`
**Commits:** `588d925` (impl + tests + example) — close-out `THIS_FILE`
**Date:** 2026-07-15
**Tests:** 230/230 vm+clox (was 221 before Stage 28, +9 stdlib tests)
**Valgrind:** clean (1164 allocs / 1164 frees in test_stdlib, 0 errors, 0 leaks)
**Bugs caught:** 0 implementation bugs, 0 test bugs, 0 example bugs. **3rd consecutive zero-bug stage (streak now at 3).**

## What shipped

`array_unique(arr) -> array` — a new native that returns a deduplicated copy of the input array, preserving first-occurrence order. Uses clox's `valuesEqual()` semantics: type-sensitive equality; for objects, identity equality (so two distinct arrays or instances are never collapsed, even if their contents match); for interned strings, content-equal literals collapse to the same pointer and are therefore treated as equal.

Implementation: walk the input once; for each element, scan the result built so far and append only if not already present. Worst case O(n²) comparisons, acceptable for the "hand-rolled dedup" lesson stage. Allocates one new `ObjArray` with capacity equal to the input count; actual count may be smaller. The result array is `push()`ed onto the VM stack before the fill loop and `pop()`ped at the end, matching the GC-protection pattern used by `string_split` (Stage 13).

## Tests added (9 total)

In `tests/test_stdlib.c`:

1. `test_array_unique_basic` — `[1, 2, 2, 3, 1]` -> `[1, 2, 3]`. First-occurrence order preserved.
2. `test_array_unique_strings` — `["a", "b", "a", "c", "b"]` -> `["a", "b", "c"]`. Interned string literals collapse by content.
3. `test_array_unique_preserves_input` — input array length and elements unchanged after the call.
4. `test_array_unique_empty` — `[]` -> `[]`.
5. `test_array_unique_single` — `[42]` -> `[42]`.
6. `test_array_unique_mixed_types` — `[1, true, "1", 1, true]` -> `[1, true, "1"]`. Distinct types are distinct values.
7. `test_array_unique_wrong_arg_count` — 0 args errors; 2 args errors. (2 asserts)
8. `test_array_unique_wrong_type` — `array_unique("hello")` errors.

## Design decisions

(1) **Return a new array; do not mutate the input.** This matches the "transform" shape of `string_pad_start` / `string_pad_end` / `string_substring` rather than the "mutate" shape of `array_reverse`. The user can always assign back if they want in-place behavior: `a = array_unique(a);`.

(2) **First-occurrence order preserved.** The obvious hand-rolled dedup loop keeps the first copy and drops later copies. Reversing that order would be surprising.

(3) **Use `valuesEqual()` semantics.** Clox's equality operator (`==`) already uses `valuesEqual`. Reusing it for deduplication means `array_unique` behaves consistently with the language's own equality. The consequence: numbers and booleans compare by value; strings compare by interned identity (which, for literal strings, is content); objects compare by identity.

(4) **O(n²) scan is acceptable for the lesson stage.** A hash-set or sort-then-scan would be faster, but this stage is about closing the "deduplicate a list" idiom, not about algorithmic sophistication. The implementation is ~30 lines and teaches the boundary between `ObjArray`, `arrayPush`, and `valuesEqual`.

(5) **GC-safety: push the result while filling.** Although `newArray(input->count)` pre-allocates enough capacity that `arrayPush` will not reallocate in the common case, the fill loop follows the same `push(OBJ_VAL(result))` / `pop()` discipline as `string_split`. This makes the native robust against future changes to allocation strategy.

## New example

`examples/unique-tags.lox` — the "deduplicate a list of tags" idiom. Composes Stage 13's `string_split` with Stage 28's `array_unique`. Demonstrates:
- basic number deduplication,
- string deduplication (interned literals collapse),
- input preservation,
- first-occurrence order,
- CSV tag deduplication,
- empty and single-element arrays.

18 example programs total.

## What this stage teaches

*The "obvious idiom the user has to hand-roll" is the trigger for a small-mirror stage.* Before Stage 28, deduplicating a clox array required a nested-loop written in Lox. The native closes that gap with the same shape as the hand-rolled version: first-occurrence order, type-sensitive equality, input untouched.

*Reuse the language's equality semantics.* `array_unique` does not invent a new equality model; it uses `valuesEqual`, the same function the `==` operator uses. The discipline: **when a native needs to compare values, start with the VM's own equality**, then document the edge cases (object identity, interned strings) rather than override them.

*GC protection is part of the contract, even when it's not strictly necessary.* The `push`/`pop` around the fill loop is two lines that prevent a latent failure if the allocation strategy changes. The cost is negligible; the benefit is that the native survives refactors of `arrayPush` or the allocator.

## What this stage does NOT teach (the limitation)

*No deep equality for objects or arrays.* Two separate `[1, 2]` arrays are treated as distinct because `valuesEqual` compares object identity. A future "deep equal" or "set by value" stage could close this, but it would require a new equality primitive and a decision about recursion depth and cycles.

*No custom comparator.* `array_unique` uses the language's built-in equality. A "unique by key" or "unique with comparator" variant is a larger design question (user-code dispatch from a native, or a language-level closure syntax). Defer to the `array_filter` / higher-order-function stage.

*O(n²) is not production-scale.* For very large arrays, the naive scan is slow. A hash-table-based `array_unique` would be faster but would require exposing the VM's hash table or adding a new dependency. This stage keeps the implementation small and dependency-free.

## My pick for Stage 29

The natural small-mirror rhythm has now run for 9 stages (21–28). The obvious small primitives left are:

1. `string_split(s, delim, limit)` — extend Stage 13's `string_split` with a max-split-count parameter. ~30 lines, no new concepts. **My pick for Stage 29.** The gap: splitting a CSV line into at most N columns, or taking only the first few tokens from a log line.
2. `array_filter(arr, predicate)` — needs user-code dispatch from a native. ~150 lines, bigger architecture. Defer.
3. Modules — ~600 lines, biggest swing. Tom's call, not mine.

After Stage 29, the small-mirror rhythm is genuinely exhausted and the trigger-mine bucket decision becomes "modules (~600 lines) or array_filter (~150 lines) or grow the bucket beyond byox."

## Aggregate test count after Stage 28

* **stdlib:** 190 (was 181 before Stage 28, +9 stdlib tests)
* **clox:** 27
* **repl:** 5
* **composability:** 8
* **vm+clox:** 230 (was 221, +9 stdlib)
* **Total on this box:** 92 (allocator) + 23 (db) + 25 (shell) + 230 (vm+clox) = **370** (was 361 before Stage 28)

## Code metrics

* **`src/native.c`:** +47 lines (impl body ~25, comment + registration ~22)
* **`tests/test_stdlib.c`:** +163 lines (9 tests + boilerplate)
* **`examples/unique-tags.lox`:** +52 lines (new)
* **Total:** +262 lines, 0 deletions

## Lessons carried into Stage 29

1. *The small-mirror rhythm is now 9 stages old.* The bucket is narrowing to "extend an existing function with one new parameter" (`string_split` with limit) rather than "add a brand-new conceptual native." Pick the next obvious idiom, not the most interesting algorithm.
2. *GC protection is part of the native contract.* Two lines (`push(OBJ_VAL(result)); ... pop();`) make the native robust against allocator changes. Apply the same pattern to any native that builds a new ObjArray in a loop.
3. *Reuse the language's equality semantics.* When a native compares values, use `valuesEqual` and document the edge cases. Do not invent a custom equality model without a strong reason.
4. *The 3-streak is real but fragile.* Stage 28 shipped with zero test failures, but a code-review pass still added the `push`/`pop` safety net. The streak measures "bugs caught by the first test run," not "absence of improvements." Keep distinguishing the two.

## What did NOT change

* `src/scanner.c`, `src/compiler.c`, `src/vm.c` — Stage 28 is a pure stdlib addition, no parser/compiler/VM changes.
* `Makefile` — no new build targets; the same 4 test binaries cover the new tests.
* The composability tests — Stage 28 doesn't add a new composability test; the `unique-tags.lox` example covers the composition implicitly.

## See also

* `docs/stage-13-closeout.md` — `string_split`, the Stage 13 native that Stage 28's example composes with.
* `docs/stage-27-closeout.md` — the previous stage (the 2-streak before Stage 28).
* `wiki/build-your-own-x.md` INDEX — the per-stage deep entry for Stage 28 will be added in the wiki update commit.
* `examples/unique-tags.lox` — the new example, demonstrating the deduplication idiom.
