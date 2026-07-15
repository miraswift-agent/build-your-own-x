# Stage 52 Close-out: array_drop(arr, n) -> array

**Stage:** 52 of 51
**Branch:** `stage-52-array-drop`
**Commit:** `adf5da9` (impl + tests + example) — close-out (this file)
**Date:** 2026-07-15
**Tests:** 404/404 (was 394 before Stage 52, +10 stdlib pass: 6 single-pass + 1 multi-subcase wrong-arg-count (2) + 1 multi-subcase wrong-type (2) = 8 functions, 10 passes total)
**Valgrind:** clean (2514 allocs / 2514 frees in test_stdlib, 0 errors, 0 leaks)
**Bugs caught:** 0 implementation bugs, 0 test bugs, 0 example bugs. **9th-consec-zero-bug stage in the new streak** (Stage 44 was 1st, Stage 45 was 2nd, Stage 46 was 3rd, Stage 47 was 4th, Stage 48 was 5th, Stage 49 was 6th, Stage 50 was 7th, Stage 51 was 8th, Stage 52 is 9th).

## What shipped

A new native `array_drop(arr, n) -> array` in `src/native.c`. 2 arguments (the array and the count). Returns a new array with the elements from index N to the end. The new wrinkle: the **"skip the first N" pattern**, the complement of Stage 51's `array_take`. The pattern is:
- If N <= 0, return the full array.
- If N >= array length, return an empty array.
- Otherwise, return the elements from index N to the end.

The source array is not mutated. **The slice operations family now has 2 variants** (array_take, array_drop).

## Tests added (8 functions, 10 sub-cases)

In `tests/test_stdlib.c`:
1. `test_array_drop_basic` — 1 sub-case: `array_drop([1, 2, 3, 4, 5], 2)` returns `[3, 4, 5]`.
2. `test_array_drop_n_equals_length` — 1 sub-case: `array_drop([1, 2, 3], 3)` returns an empty array.
3. `test_array_drop_n_exceeds_length` — 1 sub-case: `array_drop([1, 2], 10)` returns an empty array.
4. `test_array_drop_n_zero` — 1 sub-case: `array_drop([1, 2, 3], 0)` returns the full array.
5. `test_array_drop_does_not_mutate` — 1 sub-case: `array_drop(arr, 3)` doesn't mutate `arr` (arr still has 5 elements).
6. `test_array_drop_composes_with_take` — 1 sub-case: `array_take(array_drop(arr, 1), 3)` returns `[2, 3, 4]` (the sub-array starting at index 1, length 3).
7. `test_array_drop_wrong_arg_count` — 2 sub-cases: 1 arg, 3 args both error.
8. `test_array_drop_wrong_type` — 2 sub-cases: 1st arg not an array, 2nd arg not a number both error.

## Bugs caught

### 0 implementation bugs

The `arrayDropNative` function itself is correct. ~55 lines:
- argCount check (must be 2)
- Type checks (arr must be an array, n must be a number)
- N <= 0 → return the full array (cloned, not the same reference)
- N >= arr->count → return an empty array
- Otherwise → keep elements from index N to the end
- Result array pushed BEFORE the loop, popped AFTER (GC safety)

The discipline: **inspect the inputs, clamp the count, build the result, return.** No new architecture, no user-code dispatch.

### 0 test bugs

All 8 tests passed on the first run. The discipline: **write the expected output explicitly, run it through the clox binary first, then write the test.**

### 0 example bugs

All 11 example sections passed on the first run. The discipline: **verify the example output against the comments BEFORE commit** (15-occurrence pattern, the worst-class bug — clean this turn).

### Streak

**9th-consec-zero-bug stage in the new streak** (Stage 44 was 1st; Stage 44's duplicate-pop bug reset the 17-streak from Stages 27-43 to 0; Stage 44 fixed and shipped at 1; Stage 45 was 2nd; Stage 46 was 3rd; Stage 47 was 4th; Stage 48 was 5th; Stage 49 was 6th; Stage 50 was 7th; Stage 51 was 8th; Stage 52 is 9th). The new streak continues to grow.

**The new streak is now 9 stages, growing toward the 16-streak record (Stages 11-26) and the 17-streak record (Stages 27-43).** 7 more zero-bug stages would tie the 16-streak; 8 more would set a new record.

## Architecture: zero new work

Stage 52 reuses Stage 51's `array_take` pattern. The new wrinkle: **the "skip the first N" pattern, the complement of Stage 51's "take the first N" pattern.** The pattern is "if N <= 0, return full; if N >= array length, return empty; otherwise return the elements from index N to the end."

The implementation pattern is similar to Stage 51's `array_take`:
- argCount check (must be 2)
- Type checks (arr must be an array, n must be a number)
- Clamp the count to `[0, arr->count]`
- Allocate a new array of the right size
- Push to GC, loop and copy, pop from GC
- Return the new array

The "slice operations" family is now 2-strong. Future stages may add `array_take_while`, `array_drop_while`, `array_slice` (3-arg variant with start, end, step).

## Design decisions

1. **"Skip the first N pattern"** — Stage 52 introduces a native that skips the first N elements. The pattern is "if N <= 0, return full; if N >= array length, return empty; otherwise return the elements from index N to the end." The discipline: **handle the skip correctly.**
2. **"array_take and array_drop are complements"** — Stage 51 takes the first N; Stage 52 drops the first N. They form a natural pair for splitting an array into head and tail (like LISP's car + cdr, or Haskell's `splitAt`).
3. **"Stage 44's push/pop lesson was applied for the 8th consecutive stage" — 0 impl bugs in Stage 52.** The new `array_drop` registration has 1 push and 1 pop, balanced (vs. Stage 44's duplicate-pop bug). The function body has 1 push (the result) and 1 pop, balanced. The discipline: **count pushes and pops after every registration AND inside the function body.**
4. **"Test-bug discipline applied" — 0 test bugs in this turn.** All 8 tests passed on the first run. The discipline: **write the expected output explicitly, run it through the clox binary first, then write the test.**
5. **"Example-bug discipline applied" — 0 example bugs in this turn.** All 11 sections match. The discipline: **verify the example output against the comments BEFORE commit** (15-occurrence pattern, the worst-class bug).
6. **"Pagination is now elegant"** — Stage 51's example had to inline the skip logic with a loop. With `array_drop`, pagination is one line: `array_take(array_drop(items, start), page_size)`.
7. **"Type-predicate family is still COMPLETE"** — Stages 44-49 added is_array, is_string, is_number, is_bool, is_nil, is_function. The Stage 52 example doesn't use the type predicates directly, but `array_drop` itself uses `IS_ARRAY` and `IS_NUMBER` to validate inputs.

## What this stage teaches

*(a) "Skip the first N pattern" — Stage 52 introduces a native that skips the first N elements.* The pattern is "if N <= 0, return full; if N >= array length, return empty; otherwise return the elements from index N to the end." The discipline: **handle the skip correctly.**

*(b) "array_take and array_drop are complements."* Stage 51 takes the first N; Stage 52 drops the first N. They form a natural pair for splitting an array into head and tail.

*(c) "Stage 44's push/pop lesson was applied for the 8th consecutive stage" — 0 impl bugs in Stage 52.* The new `array_drop` registration has 1 push and 1 pop, balanced. The function body has 1 push and 1 pop, balanced. The discipline: **count pushes and pops after every registration AND inside the function body.** Stage 52 is 9th-consec-zero-bug in the new streak.

*(d) "Test-bug discipline applied" — 0 test bugs in this turn.* All 8 tests passed on the first run. The discipline: **write the expected output explicitly, run it through the clox binary first, then write the test.**

*(e) "Example-bug discipline applied" — 0 example bugs in this turn.* The discipline: **verify the example output against the comments BEFORE commit.**

*(f) "Slice operations family has 2 variants."* Stage 51 (array_take) + Stage 52 (array_drop). The natural next step is `array_take_while` (short-circuit) or `array_slice` (3-arg variant with start, end, step). The discipline: **slice operations are a common idiom (pagination, top-N, head/tail, sub-array extraction).**

*(g) "Pagination is now elegant."* Stage 51's example had to inline the skip logic with a loop. With `array_drop`, pagination is one line: `array_take(array_drop(items, start), page_size)`.

*(h) "Type-predicate family is still COMPLETE."* Stages 44-49 added is_array, is_string, is_number, is_bool, is_nil, is_function. The Stage 52 example doesn't use the type predicates directly, but `array_drop` itself uses `IS_ARRAY` and `IS_NUMBER` to validate inputs.

## Limitations

- **`array_drop` doesn't accept a negative-index convention** — Python-style negative N (which means "from the end") is not supported. A `array_drop(arr, -n)` variant is a future stage (or a different design).
- **`array_drop` doesn't have a `array_drop_while` variant** — only drops the first N. Future stages may add `array_drop_while` (short-circuit) and `array_take_last` (the complement of `array_drop`).
- **The new streak is at 9, still 7 short of the 16-streak record and 8 short of the 17-streak record.** Discipline: keep applying the push/pop lesson and the test-bug/example-bug disciplines.

## My pick for Stage 53

After Stage 52, the "slice operations" family has 2 variants (array_take, array_drop). The remaining candidates are:
- `array_take_while(arr, predicate) -> array` / `array_drop_while(arr, predicate) -> array` — short-circuit slice operations. **My pick for Stage 53.**
- `array_intersect(arr1, arr2) -> array` / `array_union(arr1, arr2) -> array` / `array_difference(arr1, arr2) -> array` — set operations.
- `string_pad_end(s, n, char?) -> string` — like Stage 22's string_pad_start but pads at the end.
- **Modules** — ~600 lines, biggest swing. Tom's call, not mine.
- **Apply Stages 1-52 to a different project** — the trigger-mine bucket grows beyond byox.

**My pick for Stage 53: `array_take_while(arr, predicate) -> array`** — take elements from the start of an array while the predicate is truthy. ~30 lines, low risk, reuses Stage 33's short-circuit pattern (array_any) and Stage 30's user-code-dispatch pattern. The new wrinkle: short-circuit slice (the user-code-dispatch pattern meets the slice pattern).

**After Stage 53, the next decision is one of:**
1. **`array_drop_while(arr, predicate)`** — short-circuit slice (the complement of array_take_while).
2. **`array_intersect` / `array_union` / `array_difference`** — set operations.
3. **`string_pad_end`** — like Stage 22's string_pad_start.
4. **Modules (~600 lines, Tom's call)** — the biggest swing.
5. **Apply Stages 1-52 to a different project** — the trigger-mine bucket grows beyond byox.

The next-pick for Stage 53 will be decided at the moment of decision, per the freeze-pattern antidote.

---

**File paths** (all confirmed):
- `05-vm/clox/src/native.c` — added `arrayDropNative` (~55 lines) + 12 lines registration after `array_take`
- `05-vm/clox/tests/test_stdlib.c` — added 8 test functions + 8 main() invocations (179 lines)
- `05-vm/clox/examples/array-drop.lox` — new example, 238 lines
- `05-vm/clox/docs/stage-52-closeout.md` — this file

**Branch:** `stage-52-array-drop`, commit `adf5da9` (impl + tests + example) + this file's commit.
