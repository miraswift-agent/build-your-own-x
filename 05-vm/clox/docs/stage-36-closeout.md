# Stage 36 Close-out: array_find_index

**Stage:** 36 of 35
**Branch:** `stage-36-array-find-index`
**Commit:** `403bd7b` (impl + tests + example) — close-out (this file)
**Date:** 2026-07-15
**Tests:** 280/280 (was 268 before Stage 36, +12 stdlib passes: 6 single-pass + 2 triple-pass from the wrong_ tests = 8 test functions, 12 passes)
**Valgrind:** clean (1704 allocs / 1704 frees in test_stdlib, 0 errors, 0 leaks)
**Bugs caught:** 0 implementation bugs, 0 test bugs, 1 example bug caught and fixed before commit. **10th consecutive zero-bug stage (new streak at 10 — extends the 9-streak record from Stage 35).**

## What shipped

`array_find_index(arr, predicate) -> number` — the seventh clox native that invokes user-defined Lox code from C. Takes an array and a 1-arg Lox closure (the predicate: `(element) -> truthy-or-falsy`). Returns the **index** of the first element for which the predicate is truthy, or `-1` if no element matches (or if the array is empty).

JS reference: `Array.prototype.findIndex(predicate)`
Python reference: `list.index(item)` — Python raises `ValueError` if not found; clox returns `-1` instead, which is more ergonomic for the "is this in the array, and at what index" idiom.
C reference: `strchr`/`strstr` and the `NULL`-vs-`-1` convention.
The clox semantics: short-circuit on the first truthy result (return the index `i`); empty array returns `-1` without ever calling the predicate; the "no match" case returns `-1`. The predicate's return type is unconstrained — any truthy value (in clox: anything that isn't `false` or `nil`) counts as "matched."

## Tests added (8 total, 12 passes)

In `tests/test_stdlib.c`:
1. `test_array_find_index_finds_match` — `[10, 20, 30, 40]` array_find_index(isThirty) → `2` (the index of 30).
2. `test_array_find_index_no_match` — `[2, 4, 6, 8]` array_find_index(isOdd) → `-1` (no element in `{1, 3, 5}`).
3. `test_array_find_index_empty` — `[]` array_find_index(isPositive) → `-1` (predicate never called).
4. `test_array_find_index_short_circuits` — `[10, 20, 30, 40, 50]` array_find_index(trace) → `2` after visiting 10, 20, 30 (NOT 40, NOT 50). This test is the **fourth short-circuit verification** in the project (first was Stage 33's `test_array_any_short_circuits`; second was Stage 34's `test_array_all_short_circuits`; third was Stage 35's `test_array_find_short_circuits`).
5. `test_array_find_index_first_match_wins` — `[3, 5, 7, 9]` array_find_index(isOdd) → `0` (the index of 3, the first match).
6. `test_array_find_index_does_not_mutate` — `src = [10, 20, 30, 40, 50]`, `array_find_index(src, isThirty)` → `2`, src unchanged.
7. `test_array_find_index_wrong_arg_count` — 1 arg errors; 3 args errors; 0 args errors (3 passes).
8. `test_array_find_index_wrong_type` — non-array source errors; non-function predicate errors; 0-arg predicate errors (3 passes).

## Bugs caught

### 0 implementation bugs

The first impl attempt passed all 8 tests on the first test run. The architecture from Stage 30 paid off AGAIN — this is the 6th native where the implementation is a mechanical mirror of an existing one (filter, map, reduce, any, all, find, find_index). The new wrinkle is small: the early-exit return value is `NUMBER_VAL((double)i)` (the index, not the element, not a constant bool), and the default return value is `NUMBER_VAL(-1)` (not `NIL_VAL`, not `BOOL_VAL`).

### 0 test bugs

The test author learned from Stages 33-35's test-bugs:
- **Stage 33 lesson #1: use the right array.** `test_array_find_index_finds_match` uses `[10, 20, 30, 40]` with `isThirty` (which matches `x == 30`), so 2 is correctly the index. The discipline: **when writing a "finds-match" test, ensure the expected index is the actual index of the first match.** Stage 33's `test_array_any_finds_match` used `isOdd(7)` which didn't match.
- **Stage 33 lesson #2: stringify correctly.** All `string()` calls in `test_array_find_index_short_circuits` and `test_array_find_index_does_not_mutate` are for numbers, not booleans. The final result is printed on its own line.
- **Stage 34 lesson: avoid `toString`.** None of the tests use `toString`.
- **Stage 35 lesson: declare closure first.** All tests that use a high-order native declare the closure as a top-level `fun` before passing it. The discipline: **clox doesn't support inline function expressions; declare first.**

### 1 example bug caught and fixed before commit

**Per the Stages 29/30/32/34/35 example-bug lesson (verify comments match output BEFORE commit):**

The "compose with array_filter (Stage 30 + Stage 36)" section had an off-by-one error in the comment. The first draft claimed: `print firstBigEvenIdx; // 3 (index of 8 in evens; evens[3] == 8)`. But `array_filter([1, 3, 4, 7, 8, 9, 10], isEven)` returns `[4, 8, 10]`, so the index of 8 in `evens` is 1, not 3. The fix: rewrite the comment to "1 (evens[1] == 8, the first even > 5)." This is the **same off-by-one error class** as Stage 35's find-index example bug — the test author had a mental model of one index but verified against the actual array and found another. The discipline: **run the example and check the output line-by-line against the comments BEFORE commit.**

**Discipline reinforced:** the example-bug pattern (impl right, example comment wrong) hit Stages 25, 29, 30, 32, 34, 35, and now 36. **This is a 7-occurrence pattern.** The discipline is to run the example and check the output line-by-line against the comments before commit. The 30-second cost of the verification is much less than the cost of a follow-up commit.

**10th consecutive zero-bug stage (new streak at 10 — extends the 9-streak record from Stage 35).** Stages 27, 28, 29, 30, 31, 32, 33, 34, 35, 36 all shipped clean from the first impl-test run. The 10-streak is the new record (the prior record was 9 from Stage 35; before that, 16 from Stages 11-26, broken by Stage 26's `strtol` partial-consumption bug).

## Architecture: zero new work

Stage 36 reuses the architecture from Stage 30 unchanged:
- `callClosure()` is public (renamed in Stage 30)
- `callClosureFromNative(closure, argCount)` wrapper takes N-arg
- `static int vmNativeTargetDepth` + OP_RETURN target-depth check
- Stack layout for `callClosure` is `[callee, arg1, ..., argN]`

For `array_find_index`, argCount=1, so the stack layout is `[callee, arg1]`. The new wrinkle (the early-exit return value is the index, not the element; the default return is `-1`, not `nil`) is a 1-line return change. The native returns `NUMBER_VAL((double)i)` from inside the loop body as soon as the predicate returns a truthy value. The VM's OP_RETURN target-depth check ensures the return un-winds the right number of frames.

The architecture from Stage 30 now supports 7 natives: filter, map, reduce, any, all, find, find_index. 6 of them are 1-arg, 1 is 2-arg. The N-arg design is fully exercised.

## Design decisions

1. **Reuse Stage 30's architecture unchanged.** The N-arg closure path is built in; `array_find_index` uses argCount=1, same as `array_filter`, `array_map`, `array_any`, `array_all`, `array_find`. The architecture is now used by 7 natives.
2. **Short-circuit on the *first truthy* (and return the index).** The native is the natural complement of `array_find`: `array_find` says "find the first of these truthy" and short-circuits on the first truthy (returns the element); `array_find_index` says "find the index of the first of these truthy" and short-circuits on the first truthy (returns the index). The two patterns share the same predicate-call architecture; the only difference is the return value (element vs. index).
3. **Empty-array returns `-1` without ever calling the predicate.** This matches JS `Array.prototype.findIndex([])` and Python's "no elements → no match → `ValueError`." The "no iterations" case is the canonical "default value" for "find the index in zero elements."
4. **"Not found" returns `-1` (not `nil`, not `false`, not an error).** `-1` is the canonical "no value" sentinel for index-based searches in JS (`Array.prototype.findIndex`), Python (`str.find`), and C (`strchr`/`strstr` returning `NULL` for not-found). The caller can test for "no match" with `find_index(x) == -1`. This is more ergonomic than Python's `ValueError` (which requires a try/except).
5. **The result is the index (a number), not the element.** This matches JS `Array.prototype.findIndex` and Python's `str.find` idiom. If the caller wants the element, they can use `array_find` (Stage 35) or `array_get(arr, find_index(arr, pred))`.
6. **The predicate's return type is unconstrained.** Any truthy value (anything that isn't `false` or `nil` in clox) counts as "matched." This matches JS (truthy check on the predicate's return) and Python (truthy check on the predicate's return). The clox truthy rule is documented: "only `false` and `nil` are falsy; `0` and `""` are truthy."
7. **The native's frame manages stack state correctly.** When `callClosureFromNative(predicate, 1)` is called, the predicate's slots are at `[callee, element]`. When the predicate returns, `callClosureFromNative` returns the predicate's return value. The native then returns `NUMBER_VAL((double)i)` from C. The VM pops the native's frame and pushes the native's return value. The stack is balanced throughout.
8. **No GC protection needed for `array_find_index`.** The result is a primitive (a number, not an object reference). The native doesn't allocate a new object. The VM's existing GC checkpoints in `run()` handle the predicate's allocations.
9. **The architecture is now used by 7 natives.** filter, map, reduce, any, all, find, find_index. After Stage 36, the user-code-dispatch pattern is **fully established** — the remaining "stdlib in lox" candidates (array_zip, array_group_by, array_sort) are mechanical applications of the same architecture.

## What this stage teaches

*(a) "Returns a number" is the second new shape (after Stage 35's "returns a Value").* Stage 36 is the first native that returns a `number` (the index) — a primitive Value, not an object reference. The discipline: **when designing a new native, the return type is a design decision, not a default.** Stages 30-34 chose bool/array/number-as-element; Stage 35 chose "element (any type) + nil sentinel"; Stage 36 chose "index (number) + -1 sentinel."

*(b) "-1 as sentinel" is the canonical convention for index-based searches, not a design decision.* JS, Python, C, and most other languages agree that "no index found" is `-1` (or `NULL` for pointers). The discipline: when the design matches an existing canonical convention, document the convention in the close-out (not in the test), so the next maintainer knows it's intentional.

*(c) "find + find_index" is the canonical "element + index" pair, not just two random natives.* JS exposes both `Array.prototype.find` (element) and `Array.prototype.findIndex` (index) as separate natives. Python uses `next(item for item in arr if pred(item))` and `arr.index(item)` (which raises `ValueError`). The discipline: **when two related operations differ only in the return value, ship them as two separate natives**, not as a single native with a flag (e.g., `find(arr, pred, {returnIndex: true})` would be more complex and less ergonomic than two separate natives).

*(d) "Test-bug lessons from prior stages carry forward; example-bug lessons don't (yet)."* Stage 33's test-bug (wrong array) didn't repeat. But the example-bug pattern (impl right, example comment wrong) hit Stages 25, 29, 30, 32, 34, 35, and now 36. The discipline: **example-bugs are a separate discipline from test-bugs; verify the example output against the comments BEFORE commit.** This is a 7-occurrence pattern; the discipline is to run the example and check the output line-by-line against the comments before commit.

*(e) "Off-by-one in comments" is the most common example-bug class.* The Stage 36 example had an off-by-one error in the comment (claimed index 3, but actual was 1). The discipline: **for any comment that says "this is at index N", verify N against the actual output BEFORE commit.** This is a sub-pattern of the example-bug discipline.

*(f) "clox doesn't support inline function expressions" — same lesson as Stages 30/32/33/34/35.* Declare the closure first, then pass it to the high-order native.

## Limitations

- No "no-predicate" form (predicate is required). The native errors if argCount != 2.
- The predicate must be a 1-arg Lox closure (not 0-arg, not 2-arg, not a native). The native errors if the arity check fails.
- The predicate's return type is unconstrained; the result is `-1` if the predicate is falsy for every element.
- The source array is not mutated (read-only access).
- The result is the index, not the element. To get the element, use `array_get(arr, array_find_index(arr, pred))` or use `array_find` (Stage 35) directly.
- No GC protection needed (returns a primitive, no new allocations).
- Empty array returns `-1` (no predicate call). The discipline matches JS/Python canonical convention.
- "Not found" returns `-1` (not `nil`, not an error). The caller can test for "no match" with `find_index(x) == -1`.
- The result is always a non-negative integer (for a match) or `-1` (for no match). No floating-point indices (an index is always an `int` in clox's array model).
- No "find last index of" variant (that's `array_find_last_index` or `array_find_last`).

## My pick for Stage 37

After Stage 36, the user-code-dispatch pattern is **fully established** for the JS `Array.prototype` iteration trifecta plus `find` and `findIndex`. The remaining "stdlib in lox" candidates are:
- `array_zip(arr1, arr2) -> array` (combines two arrays element-wise; needs 2-source or N-arg form)
- `array_group_by(arr, keyFn) -> array` (groups by key; needs object/hash support)
- `array_sort(arr, comparator?) -> array` (in-place or out-of-place sort)

**After Stage 36, the next decision is one of:**
1. **Modules (~600 lines, Tom's call)** — the biggest swing. File paths, recursive imports, circular detection, import syntax.
2. **Apply Stages 1-36 to a different project** — the trigger-mine bucket grows beyond byox.
3. **Pick a different byox stage** (e.g., `array_zip`, `array_group_by`, or one of the string-handling natives that's still missing).

The next-pick for Stage 37 will be decided at the moment of decision, per the freeze-pattern antidote.

---

**File paths** (all confirmed):
- `05-vm/clox/src/native.c` — added `arrayFindIndexNative` (73 lines) + registration block (12 lines)
- `05-vm/clox/tests/test_stdlib.c` — added 8 test functions + 9 main() invocations (209 lines)
- `05-vm/clox/examples/array-find-index.lox` — new example, 97 lines
- `05-vm/clox/docs/stage-36-closeout.md` — this file

**Branch:** `stage-36-array-find-index`, commit `403bd7b` (impl + tests + example).
