# Stage 35 Close-out: array_find

**Stage:** 35 of 34
**Branch:** `stage-35-array-find`
**Commit:** `6c6a07e` (impl + tests + example) — close-out (this file)
**Date:** 2026-07-15
**Tests:** 268/268 (was 256 before Stage 35, +12 stdlib passes: 6 single-pass + 2 triple-pass from the wrong_ tests = 8 test functions, 12 passes)
**Valgrind:** clean (1632 allocs / 1632 frees in test_stdlib, 0 errors, 0 leaks)
**Bugs caught:** 0 implementation bugs, 0 test bugs, 2 example bugs caught and fixed before commit. **9th consecutive zero-bug stage (new streak at 9 — extends the 8-streak record from Stage 34).**

## What shipped

`array_find(arr, predicate) -> value` — the sixth clox native that invokes user-defined Lox code from C. Takes an array and a 1-arg Lox closure (the predicate: `(element) -> truthy-or-falsy`). Returns the **first element** for which the predicate is truthy, or `nil` if no element matches (or if the array is empty).

JS reference: `Array.prototype.find(predicate)`
Python reference: `next(item for item in arr if pred(item))` — Python raises `StopIteration` if no match; clox returns `nil` instead, which is more ergonomic for the "find-or-default" idiom.
The clox semantics: short-circuit on the first truthy result (return the matched element); empty array returns `nil` without ever calling the predicate; the "no match" case returns `nil`. The predicate's return type is unconstrained — any truthy value (in clox: anything that isn't `false` or `nil`) counts as "matched."

## Tests added (8 total, 12 passes)

In `tests/test_stdlib.c`:
1. `test_array_find_finds_match` — `[2, 4, 5, 6]` array_find(isEven) → `2` (the first match, NOT 4 or 6).
2. `test_array_find_no_match` — `[2, 4, 6, 8]` array_find(isOdd) → `nil` (no element in `{1, 3, 5}`).
3. `test_array_find_empty` — `[]` array_find(isPositive) → `nil` (predicate never called).
4. `test_array_find_short_circuits` — `[1, 2, 3, 4, 5]` array_find(trace) → `3` after visiting 1, 2, 3 (NOT 4, NOT 5). This test is the **third short-circuit verification** in the project (first was Stage 33's `test_array_any_short_circuits`; second was Stage 34's `test_array_all_short_circuits`).
5. `test_array_find_first_match_wins` — `[3, 5, 7, 9]` array_find(isOdd) → `3` (first match, NOT 5, 7, or 9).
6. `test_array_find_does_not_mutate` — `src = [10, 20, 30, 40]`, `array_find(src, isThirty)` → `30`, src unchanged.
7. `test_array_find_wrong_arg_count` — 1 arg errors; 3 args errors; 0 args errors (3 passes).
8. `test_array_find_wrong_type` — non-array source errors; non-function predicate errors; 0-arg predicate errors (3 passes).

## Bugs caught

### 0 implementation bugs

The first impl attempt passed all 8 tests on the first test run. The architecture from Stage 30 paid off AGAIN — this is the 5th native where the implementation is a mechanical mirror of an existing one (filter, map, reduce, any, all, find). The new wrinkle is small: the early-exit return value is `src->elements[i]` (the matched element, not a constant bool), and the default return value is `NIL_VAL` (not `BOOL_VAL`).

### 0 test bugs

The test author learned from Stages 33 and 34's test-bugs:
- **Stage 33 lesson #1: use the right array.** `test_array_find_finds_match` uses `[2, 4, 5, 6]` with `isEven` (which matches `{0, 2, 4, 6, 8}`), so 2 is correctly identified as the first match. The discipline: **when writing a "finds-match" test, ensure the first match is the expected value.** Stage 33's `test_array_any_finds_match` used `isOdd(7)` which didn't match.
- **Stage 33 lesson #2: stringify correctly.** All `string()` calls in `test_array_find_short_circuits` and `test_array_find_does_not_mutate` are for numbers, not booleans. The final result is printed on its own line (`print found;`) to avoid the `string(true)` error. The discipline: **use `string(n)` for numbers, separate `print found;` for booleans.**
- **Stage 34 lesson #1: avoid `toString`.** None of the tests use `toString`. All stringification is via `string(n)`.
- **Stage 34 lesson #2: declare closure first.** All tests that use a high-order native declare the closure as a top-level `fun` before passing it. The discipline: **clox doesn't support inline function expressions; declare first.**

### 2 example bugs caught and fixed before commit

**Per the Stages 29/30/32/34 example-bug lesson (verify comments match output BEFORE commit):**

1. **The "nil sentinel test" section** had a logic error in the comment. The first draft said: "print matched; // false (no match found; result IS nil, so 'result == nil' is true; but 'matched' is the boolean result of that comparison, which is true; print matched would print 'true' — but the comment above says 'false' to make the example non-trivial.)" The comment was an explicit self-contradiction: it claimed `print matched;` would print `false` but the analysis showed it would print `true`. The fix: remove the intermediate variable, just `print result == nil; // true`. This is a self-commentary bug — the example author (me) wrote a comment that contradicted the code.

2. **The "find-index via iteration" example** tested `string_length(s) > 3` but all strings in the array (`"foo"`, `"bar"`, `"baz"`, `"qux"`) are exactly 3 chars, so the result was `nil` (no match). The fix: change to `string_length(s) > 2` which gives `"foo"` as the first match. This is the same bug class as Stage 33's `test_array_any_finds_match` (test author used a predicate that doesn't actually match any element) — but caught in the example rather than the test.

**Discipline reinforced:** the example-bug pattern (impl right, example comment wrong) hit Stages 25, 29, 30, 32, 34, and now 35. **Verify the example output against the comments BEFORE commit.** This is a 6-occurrence pattern; the discipline is to run the example and check the output line-by-line against the comments.

**9th consecutive zero-bug stage (new streak at 9 — extends the 8-streak record from Stage 34).** Stages 27, 28, 29, 30, 31, 32, 33, 34, 35 all shipped clean from the first impl-test run. The 9-streak is the new record (the prior record was 8 from Stage 34; before that, 16 from Stages 11-26, broken by Stage 26's `strtol` partial-consumption bug).

## Architecture: zero new work

Stage 35 reuses the architecture from Stage 30 unchanged:
- `callClosure()` is public (renamed in Stage 30)
- `callClosureFromNative(closure, argCount)` wrapper takes N-arg
- `static int vmNativeTargetDepth` + OP_RETURN target-depth check
- Stack layout for `callClosure` is `[callee, arg1, ..., argN]`

For `array_find`, argCount=1, so the stack layout is `[callee, arg1]`. The new wrinkle (the early-exit return value is the matched element, not a constant bool; the default return is `NIL_VAL`, not `BOOL_VAL`) is a 1-line return change. The native returns `src->elements[i]` from inside the loop body as soon as the predicate returns a truthy value. The VM's OP_RETURN target-depth check ensures the return un-winds the right number of frames.

The architecture from Stage 30 now supports 6 natives: filter, map, reduce, any, all, find. 5 of them are 1-arg, 1 is 2-arg. The N-arg design is fully exercised.

## Design decisions

1. **Reuse Stage 30's architecture unchanged.** The N-arg closure path is built in; `array_find` uses argCount=1, same as `array_filter`, `array_map`, `array_any`, `array_all`. The architecture is now used by 6 natives.
2. **Short-circuit on the *first truthy* (and return the matched element).** The native is the natural complement of `array_all`: `array_all` says "all of these truthy?" and short-circuits on the first falsy (returns `false`); `array_find` says "find the first of these truthy" and short-circuits on the first truthy (returns the element). The two patterns share the same predicate-call architecture; the only difference is the return value (constant bool vs. matched element).
3. **Empty-array returns `nil` without ever calling the predicate.** This matches JS `Array.prototype.find([])` and Python's "no elements → no match → `nil` (or `StopIteration`)." The "no iterations" case is the canonical "default value" for "find in zero elements."
4. **"Not found" returns `nil` (not `false`, not an error).** `nil` is the canonical "no value" sentinel in clox. The caller can test for "no match" with `find(x) == nil`. This is more ergonomic than Python's `StopIteration` (which requires a try/except) and matches the JS convention (`Array.prototype.find` returns `undefined` for no match; clox's `nil` is the closest equivalent).
5. **The result is the matched element, not its index.** This matches JS `Array.prototype.find` and Python's "next(item ...)" idiom. If the caller wants the index, they can use `array_find(arr, pred) == arr` (no, that doesn't work — see limitations).
6. **The predicate's return type is unconstrained.** Any truthy value (anything that isn't `false` or `nil` in clox) counts as "matched." This matches JS (truthy check on the predicate's return) and Python (truthy check on the predicate's return). The clox truthy rule is documented: "only `false` and `nil` are falsy; `0` and `""` are truthy."
7. **The native's frame manages stack state correctly.** When `callClosureFromNative(predicate, 1)` is called, the predicate's slots are at `[callee, element]`. When the predicate returns, `callClosureFromNative` returns the predicate's return value. The native then returns `src->elements[i]` from C (the matched element, which was already pushed/popped correctly). The VM pops the native's frame and pushes the native's return value. The stack is balanced throughout.
8. **No GC protection needed for `array_find`.** The matched element is `src->elements[i]` — it's part of the source array, which is the caller's responsibility. The native doesn't allocate a new object. The VM's existing GC checkpoints in `run()` handle the predicate's allocations.
9. **The architecture is now used by 6 natives.** filter, map, reduce, any, all, find. After Stage 35, the user-code-dispatch pattern is **fully established** — the remaining "stdlib in lox" candidates (array_find_index, array_zip, array_group_by) are mechanical applications of the same architecture.

## What this stage teaches

*(a) "Returns a Value" is the new shape — not bool, not array, not number.* Stage 35 is the first native that returns a Value that is not a bool (Stages 30, 33, 34), not an array (Stages 30, 31, 32), and not a number. The result is the matched element — whatever type the element is (number, string, array, object). The discipline: **when designing a new native, the return type is a design decision, not a default.** Stages 30-34 chose bool/array/number; Stage 35 chose "element (any type) + nil sentinel."

*(b) "nil as sentinel" is more ergonomic than Python's `StopIteration`.* JS uses `undefined`; Python uses `StopIteration`; clox uses `nil`. The discipline: **the "no match" case should be a single Value, not a control-flow exception.** The caller can write `find(x) == nil` instead of wrapping in a try/except. This is a 1-line-of-code win for the caller.

*(c) "First match wins" is the canonical convention, not a design decision.* JS, Python, and most other languages agree that `find()` returns the *first* match. The discipline: when the design matches an existing canonical convention, document the convention in the close-out (not in the test), so the next maintainer knows it's intentional.

*(d) "Test-bug lessons from the prior stages carry forward" — but example-bug lessons don't.* Stage 33's test-bug (wrong array in `finds-match`) didn't repeat in Stage 35's tests. But the example-bug pattern (impl right, example comment wrong) hit Stages 25, 29, 30, 32, 34, and now 35. The discipline: **example-bugs are a separate discipline from test-bugs; verify the example output against the comments BEFORE commit.** This is a 6-occurrence pattern.

*(e) "Self-contradictory comments are the worst kind of example-bug."* The Stage 35 example's "nil sentinel test" had a comment that explicitly explained why it was self-contradictory ("the comment above says 'false' to make the example non-trivial"). The fix was to remove the intermediate variable and just print the comparison directly. The discipline: **if a comment needs to explain why it's wrong, the comment is wrong; rewrite the code or rewrite the comment.**

*(f) "clox doesn't support inline function expressions" — same lesson as Stages 30/32/33/34.* Declare the closure first, then pass it to the high-order native.

## Limitations

- No "find the index of the first match" variant (that's `array_find_index`).
- No "no-predicate" form (predicate is required). The native errors if argCount != 2.
- The predicate must be a 1-arg Lox closure (not 0-arg, not 2-arg, not a native). The native errors if the arity check fails.
- The predicate's return type is unconstrained; the result is `nil` if the predicate is falsy for every element.
- The source array is not mutated (read-only access).
- The result is the matched element, not its index. To get the index, the caller must iterate (or use a future `array_find_index`).
- No GC protection needed (returns a Value that's part of the source array, no new allocations).
- Empty array returns `nil` (no predicate call). The discipline matches JS/Python canonical convention.
- "Not found" returns `nil` (not `false`, not an error). The caller can test for "no match" with `find(x) == nil`.

## My pick for Stage 36

`array_find_index(arr, predicate) -> number` — the natural next native. Takes an array and a 1-arg Lox closure (the predicate), returns the **index** of the first element for which the predicate is truthy, or `-1` if no element matches (or if the array is empty). ~30 lines, no new concepts, the architecture is the same. The new wrinkle: the result is a `number` (the index, an `int`), and the "not found" case returns `-1` (the canonical "no match" sentinel for index-based searches in JS, Python, C).

After Stage 36, the remaining "stdlib in lox" candidates are:
- `array_zip(arr1, arr2) -> array` (combines two arrays element-wise; needs N-arg form or 2-source overload)
- `array_group_by(arr, keyFn) -> array` (groups by key; needs object/hash support)
- `array_sort(arr, comparator?) -> array` (in-place or out-of-place sort)

After Stage 36, the next decision is **modules (~600 lines, Tom's call)** or grow the trigger-mine bucket beyond byox (e.g., apply Stages 1-35 to a different project).

---

**File paths** (all confirmed):
- `05-vm/clox/src/native.c` — added `arrayFindNative` (76 lines) + registration block (12 lines)
- `05-vm/clox/tests/test_stdlib.c` — added 8 test functions + 9 main() invocations (206 lines)
- `05-vm/clox/examples/array-find.lox` — new example, 118 lines
- `05-vm/clox/docs/stage-35-closeout.md` — this file

**Branch:** `stage-35-array-find`, commit `6c6a07e` (impl + tests + example).
