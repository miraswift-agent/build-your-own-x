# Stage 51 Close-out: array_take(arr, n) -> array

**Stage:** 51 of 50
**Branch:** `stage-51-array-take`
**Commit:** `a2cb675` (impl + tests + example) — close-out (this file)
**Date:** 2026-07-15
**Tests:** 394/394 (was 385 before Stage 51, +9 stdlib pass: 5 single-pass + 1 multi-subcase wrong-arg-count (2) + 1 multi-subcase wrong-type (2) = 7 functions, 9 passes total)
**Valgrind:** clean (2454 allocs / 2454 frees in test_stdlib, 0 errors, 0 leaks)
**Bugs caught:** 0 implementation bugs, 0 test bugs, **3 example bugs** (fixed BEFORE commit). **8th-consec-zero-bug stage in the new streak** (Stage 44 was 1st, Stage 45 was 2nd, Stage 46 was 3rd, Stage 47 was 4th, Stage 48 was 5th, Stage 49 was 6th, Stage 50 was 7th, Stage 51 is 8th).

## What shipped

A new native `array_take(arr, n) -> array` in `src/native.c`. 2 arguments (the array and the count). Returns a new array with the first N elements. The new wrinkle: a **"size limit" pattern**, similar to Stage 41's `array_chunk`. The pattern is:
- If N <= 0, return an empty array.
- If N >= array length, return the full array.
- Otherwise, return the first N elements.

The source array is not mutated. **The "slice operations" family begins** with Stage 51.

## Tests added (7 functions, 9 sub-cases)

In `tests/test_stdlib.c`:
1. `test_array_take_basic` — 1 sub-case: `array_take([1, 2, 3, 4, 5], 3)` returns `[1, 2, 3]`.
2. `test_array_take_n_equals_length` — 1 sub-case: `array_take([1, 2, 3], 3)` returns the full array `[1, 2, 3]`.
3. `test_array_take_n_exceeds_length` — 1 sub-case: `array_take([1, 2], 10)` returns the full array `[1, 2]`.
4. `test_array_take_n_zero` — 1 sub-case: `array_take([1, 2, 3], 0)` returns an empty array.
5. `test_array_take_does_not_mutate` — 1 sub-case: `array_take(arr, 2)` doesn't mutate `arr` (arr still has 5 elements).
6. `test_array_take_wrong_arg_count` — 2 sub-cases: 1 arg, 3 args both error.
7. `test_array_take_wrong_type` — 2 sub-cases: 1st arg not an array, 2nd arg not a number both error.

## Bugs caught

### 0 implementation bugs

The `arrayTakeNative` function itself is correct. ~46 lines:
- argCount check (must be 2)
- Type checks (arr must be an array, n must be a number)
- N <= 0 → return empty array
- N >= arr->count → take all elements
- Otherwise → take min(N, arr->count) elements
- Result array pushed BEFORE the loop, popped AFTER (GC safety)

The discipline: **inspect the inputs, clamp the count, build the result, return.** No new architecture, no user-code dispatch.

### 0 test bugs

All 7 tests passed on the first run. The discipline: **write the expected output explicitly, run it through the clox binary first, then write the test.**

### 3 example bugs (fixed BEFORE commit)

**Example bug #1: used `array_drop`, which doesn't exist yet.** The pagination example used `array_take(array_drop(items, start), page_size)`, but `array_drop` is the future Stage 52. The fix was to inline the skip logic with a loop.

**Example bug #2: pagination function was wrong.** The first version of `page()` used `array_take(array_take(items, start + take_count), take_count)`, which ignored the `start` parameter. The fix was to use a loop to build the result array element-by-element.

**Example bug #3: `array_push()` returns the new LENGTH, not the array.** The pagination function originally had `result = array_push(result, ...)`, which overwrites `result` with a number. (Stage 20 lesson: `array_push` returns the new length, not the array.) The fix was to use `array_push(result, ...)` (no assignment, since `array_push` mutates in place).

**18th-20th example-bugs in 51-stage history.** The example-bug discipline (15-occurrence pattern) was applied: 3 bugs caught BEFORE commit, not during a test cycle. The discipline: **verify the example output against the comments BEFORE commit.**

### Streak

**8th-consec-zero-bug stage in the new streak** (Stage 44 was 1st; Stage 44's duplicate-pop bug reset the 17-streak from Stages 27-43 to 0; Stage 44 fixed and shipped at 1; Stage 45 was 2nd; Stage 46 was 3rd; Stage 47 was 4th; Stage 48 was 5th; Stage 49 was 6th; Stage 50 was 7th; Stage 51 is 8th). The new streak continues to grow.

The test-bugs and example-bugs don't break the impl-bug streak (the impl is correct, the tests/example were wrong). **8 stages in a row with zero implementation bugs** is a strong signal that the push/pop discipline and the "test-bug caught mid-flight" pattern are working.

## Architecture: zero new work

Stage 51 reuses Stage 32's `array_filter` and Stage 41's `array_chunk` patterns. The new wrinkle: **a "size limit" pattern, similar to Stage 41's `array_chunk`.** The pattern is "if N <= 0, return empty; if N >= array length, return full; otherwise return the first N elements."

The implementation pattern is similar to Stage 32's `array_filter`:
- argCount check (must be 2)
- Type checks (arr must be an array, n must be a number)
- Clamp the count to `[0, arr->count]`
- Allocate a new array of the right size
- Push to GC, loop and copy, pop from GC
- Return the new array

The "size limit" family is small but reusable: future stages may add `array_drop`, `array_take_while`, `array_drop_while`, `array_slice` (3-arg variant). **Stage 51 is the first of the slice operations.**

## Design decisions

1. **"Size limit pattern"** — Stage 51 introduces a native with a "limit" parameter. The pattern is "if N <= 0, return empty; if N >= array length, return full; otherwise return the first N elements." The discipline: **handle the limit correctly.**
2. **"Stage 44's push/pop lesson was applied for the 7th consecutive stage" — 0 impl bugs in Stage 51.** The new `array_take` registration has 1 push and 1 pop, balanced (vs. Stage 44's duplicate-pop bug). The function body has 1 push (the result) and 1 pop, balanced. The discipline: **count pushes and pops after every registration AND inside the function body.**
3. **"Test-bug discipline applied" — 0 test bugs in this turn.** All 7 tests passed on the first run. The discipline: **write the expected output explicitly, run it through the clox binary first, then write the test.**
4. **"Example-bug discipline applied" — 3 example bugs caught and fixed BEFORE commit.** The 3 bugs (array_drop doesn't exist, pagination function was wrong, array_push returns length not array) were all caught by **verifying the example output against the comments BEFORE commit** (15-occurrence pattern, the worst-class bug). The discipline: **always run the example through the clox binary before commit, and compare the output line-by-line to the comments.**
5. **"Slice operations family begins"** — Stage 51 is the first of the slice operations. Future stages may add `array_drop` (the complement), `array_take_while` (short-circuit), `array_drop_while` (short-circuit), `array_slice` (3-arg variant with start, end, step). The discipline: **slice operations are a common idiom (pagination, top-N, batch processing).**
6. **"Pagination is a common idiom"** — the example includes a `page(items, page_size, page_num)` function that uses `array_take` to extract a page. The current implementation uses a loop to skip the first N elements; a future `array_drop` native would make it more elegant: `array_take(array_drop(items, start), page_size)`.
7. **"Type-predicate family is now COMPLETE"** — Stages 44-49 added is_array, is_string, is_number, is_bool, is_nil, is_function. The Stage 51 example doesn't use the type predicates directly, but `array_take` itself uses `IS_ARRAY` and `IS_NUMBER` to validate inputs.

## What this stage teaches

*(a) "Size limit pattern" — Stage 51 introduces a native with a "limit" parameter.* The pattern is "if N <= 0, return empty; if N >= array length, return full; otherwise return the first N elements." The discipline: **handle the limit correctly.**

*(b) "Stage 44's push/pop lesson was applied for the 7th consecutive stage" — 0 impl bugs in Stage 51.* The new `array_take` registration has 1 push and 1 pop, balanced. The function body has 1 push and 1 pop, balanced. The discipline: **count pushes and pops after every registration AND inside the function body.**

*(c) "Test-bug discipline applied" — 0 test bugs in this turn.* All 7 tests passed on the first run. The discipline: **write the expected output explicitly, run it through the clox binary first, then write the test.**

*(d) "Example-bug discipline applied" — 3 example bugs caught and fixed BEFORE commit.* The 3 bugs (array_drop doesn't exist, pagination function was wrong, array_push returns length not array) were all caught by verifying the example output against the comments. The discipline: **always run the example through the clox binary before commit, and compare the output line-by-line to the comments.**

*(e) "Slice operations family begins."* Stage 51 is the first of the slice operations. Future stages may add `array_drop`, `array_take_while`, `array_drop_while`, `array_slice`. The discipline: **slice operations are a common idiom (pagination, top-N, batch processing).**

*(f) "Pagination is a common idiom."* The example includes a `page(items, page_size, page_num)` function. The current implementation uses a loop to skip the first N elements; a future `array_drop` native would make it more elegant.

*(g) "Type-predicate family is now COMPLETE."* Stages 44-49 added is_array, is_string, is_number, is_bool, is_nil, is_function. The Stage 51 example doesn't use the type predicates directly, but `array_take` itself uses `IS_ARRAY` and `IS_NUMBER` to validate inputs.

## Limitations

- **`array_take` doesn't accept a negative-index convention** — Python-style negative N (which means "from the end") is not supported. A `array_take(arr, -n)` variant is a future stage (or a different design).
- **`array_take` doesn't have a `array_take_while` / `array_take_last` variant** — only takes the first N. Future stages may add `array_take_while` (short-circuit) and `array_take_last` (the complement of `array_drop`).
- **The example-bugs were caught pre-commit** — but the patterns (15 example-bugs) show they're frequent hazards. The discipline: always verify before commit.
- **The pagination example uses a loop instead of `array_drop`** — a future `array_drop` native would make this more elegant. This is a known limitation, not a bug.

## My pick for Stage 52

After Stage 51, the "slice operations" family has 1 variant (array_take). The remaining candidates are:
- `array_drop(arr, n) -> array` — drop the first N elements (the complement of array_take). **My pick for Stage 52.**
- `array_take_while(arr, predicate) -> array` / `array_drop_while(arr, predicate) -> array` — short-circuit slice operations.
- `array_intersect(arr1, arr2) -> array` / `array_union(arr1, arr2) -> array` / `array_difference(arr1, arr2) -> array` — set operations.
- `string_pad_end(s, n, char?) -> string` — like Stage 22's string_pad_start but pads at the end.
- **Modules** — ~600 lines, biggest swing. Tom's call, not mine.
- **Apply Stages 1-51 to a different project** — the trigger-mine bucket grows beyond byox.

**My pick for Stage 52: `array_drop(arr, n) -> array`** — drop the first N elements of an array. ~25 lines, low risk, reuses Stage 51's array-building pattern. The new wrinkle: the "skip the first N" pattern, the complement of Stage 51's "take the first N" pattern.

**After Stage 52, the next decision is one of:**
1. **`array_take_while` / `array_drop_while`** — short-circuit slice operations.
2. **`array_intersect` / `array_union` / `array_difference`** — set operations.
3. **`string_pad_end`** — like Stage 22's string_pad_start.
4. **Modules (~600 lines, Tom's call)** — the biggest swing.
5. **Apply Stages 1-51 to a different project** — the trigger-mine bucket grows beyond byox.

The next-pick for Stage 52 will be decided at the moment of decision, per the freeze-pattern antidote.

---

**File paths** (all confirmed):
- `05-vm/clox/src/native.c` — added `arrayTakeNative` (~46 lines) + 12 lines registration after `array_zip_longest`
- `05-vm/clox/tests/test_stdlib.c` — added 7 test functions + 7 main() invocations (158 lines)
- `05-vm/clox/examples/array-take.lox` — new example, 209 lines
- `05-vm/clox/docs/stage-51-closeout.md` — this file

**Branch:** `stage-51-array-take`, commit `a2cb675` (impl + tests + example) + this file's commit.
