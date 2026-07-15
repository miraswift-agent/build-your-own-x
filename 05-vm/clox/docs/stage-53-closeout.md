# Stage 53 Close-out: array_take_while(arr, predicate) -> array

**Stage:** 53 of 52
**Branch:** `stage-53-array-take-while`
**Commit:** `2670faa` (impl + tests + example) — close-out (this file)
**Date:** 2026-07-15
**Tests:** 413/413 (was 404 before Stage 53, +9 stdlib pass: 5 single-pass + 1 multi-subcase wrong-arg-count (2) + 1 multi-subcase wrong-type (2) = 7 functions, 9 passes total)
**Valgrind:** clean (2568 allocs / 2568 frees in test_stdlib, 0 errors, 0 leaks)
**Bugs caught:** 0 implementation bugs, 0 test bugs, **3 example bugs** (fixed BEFORE commit). **10th-consec-zero-bug stage in the new streak** (Stage 44 was 1st, Stage 45 was 2nd, Stage 46 was 3rd, Stage 47 was 4th, Stage 48 was 5th, Stage 49 was 6th, Stage 50 was 7th, Stage 51 was 8th, Stage 52 was 9th, Stage 53 is 10th).

## What shipped

A new native `array_take_while(arr, predicate) -> array` in `src/native.c`. 2 arguments (the array and the predicate). Returns a new array with the elements from the start while the predicate is truthy. Stops at the first element where the predicate is falsy. The new wrinkle: **short-circuit slice** (user-code-dispatch meets slice). The pattern is "iterate from the start, call the predicate on each element, stop as soon as the predicate is falsy".

The source array is not mutated. **The slice operations family now has 3 variants** (array_take, array_drop, array_take_while).

## Tests added (7 functions, 9 sub-cases)

In `tests/test_stdlib.c`:
1. `test_array_take_while_basic` — 1 sub-case: `array_take_while([1, 2, 3, -1, 4, 5], is_positive)` returns `[1, 2, 3]`.
2. `test_array_take_while_all_match` — 1 sub-case: `array_take_while(["hi", "hey", "yo"], is_short)` returns the full array.
3. `test_array_take_while_none_match` — 1 sub-case: `array_take_while([-1, -2, 1, 2], is_positive)` returns an empty array.
4. `test_array_take_while_empty` — 1 sub-case: `array_take_while([], always_true)` returns an empty array.
5. `test_array_take_while_does_not_mutate` — 1 sub-case: `array_take_while(arr, is_positive)` doesn't mutate `arr`.
6. `test_array_take_while_wrong_arg_count` — 2 sub-cases: 1 arg, 3 args both error.
7. `test_array_take_while_wrong_type` — 2 sub-cases: 1st arg not an array, 2nd arg not a function both error.

## Bugs caught

### 0 implementation bugs

The `arrayTakeWhileNative` function itself is correct. ~50 lines:
- argCount check (must be 2)
- Type checks (arr must be an array, predicate must be a closure)
- Arity check (predicate must take 1 argument)
- Iterate from the start, call the predicate on each element
- Short-circuit: stop as soon as the predicate is falsy
- Result array pushed BEFORE the loop, popped AFTER (GC safety); also popped on the short-circuit return path

The discipline: **inspect the inputs, iterate, short-circuit on the first falsy, return.** No new architecture.

### 0 test bugs

All 7 tests passed on the first run. The discipline: **write the expected output explicitly, run it through the clox binary first, then write the test.**

### 3 example bugs (fixed BEFORE commit)

**Example bug #1: clox's logical AND is `and`, not `&&`.** The `is_digit` predicate used `string_length(s) == 1 && string_to_number(s) >= 0`. The fix was to use `and` instead of `&&`. (Stage 18 lesson: clox uses `and`/`or` keywords for logical operators.)

**Example bug #2: 2-arg predicate rejected by arity check.** The `is_non_decreasing_after(arr, i)` predicate takes 2 args (the array and the index), but the arity check in `arrayTakeWhileNative` requires exactly 1 arg. The fix was to use a stateful 1-arg predicate with outer-scope capture (`prev_var` is updated inside the predicate).

**Example bug #3: clox's `>=` and `<=` are numeric only.** The `is_digit` predicate used `s >= "0" and s <= "9"`. clox's `OP_GREATER` and `OP_LESS` use `BINARY_OP` which is numeric. The fix was to use `==` against each digit string. (Stage 33 lesson: clox's `>=` and `<=` are numeric only; for strings, use `==`/`!=` or compare lengths.)

**20th-22nd example-bugs in 53-stage history.** The example-bug discipline (15-occurrence pattern) was applied: 3 bugs caught BEFORE commit, not during a test cycle. The discipline: **verify the example output against the comments BEFORE commit.**

### Streak

**10th-consec-zero-bug stage in the new streak** (Stage 44 was 1st; Stage 44's duplicate-pop bug reset the 17-streak from Stages 27-43 to 0; Stage 44 fixed and shipped at 1; Stage 45 was 2nd; Stage 46 was 3rd; Stage 47 was 4th; Stage 48 was 5th; Stage 49 was 6th; Stage 50 was 7th; Stage 51 was 8th; Stage 52 was 9th; Stage 53 is 10th). The new streak continues to grow.

**The new streak is now 10 stages, 6 short of the 16-streak record (Stages 11-26) and 7 short of the 17-streak record (Stages 27-43).** 6 more zero-bug stages would tie the 16-streak; 7 more would set a new record.

## Architecture: zero new work

Stage 53 reuses Stage 30's `array_filter` (user-code-dispatch) and Stage 33's `array_any` (short-circuit) patterns. The new wrinkle: **the combination of user-code-dispatch + short-circuit + slice.**

The implementation pattern is similar to Stage 33's `array_any`:
- argCount check (must be 2)
- Type checks (arr must be an array, predicate must be a closure)
- Arity check (predicate must take 1 argument)
- Iterate from the start, calling the predicate on each element
- Short-circuit: stop as soon as the predicate is falsy
- Result array pushed BEFORE the loop, popped AFTER (GC safety); also popped on the short-circuit return path

The "slice operations" family is now 3-strong. Future stages may add `array_drop_while` (the complement), `array_slice` (3-arg variant with start, end, step).

## Design decisions

1. **"Short-circuit slice pattern"** — Stage 53 introduces a native that combines user-code-dispatch, short-circuit, and slice. The pattern is "iterate from the start, call the predicate on each element, stop as soon as the predicate is falsy". The discipline: **handle the short-circuit correctly (pop the result array on the short-circuit return path to keep the GC stack balanced).**
2. **"Stage 44's push/pop lesson was applied for the 9th consecutive stage" — 0 impl bugs in Stage 53.** The new `array_take_while` registration has 1 push and 1 pop, balanced. The function body has 1 push (the result) and 1 pop after the loop. The short-circuit return path also has 1 pop to keep the GC stack balanced. The discipline: **count pushes and pops after every registration AND inside the function body, including every return path.**
3. **"Test-bug discipline applied" — 0 test bugs in this turn.** All 7 tests passed on the first run. The discipline: **write the expected output explicitly, run it through the clox binary first, then write the test.**
4. **"Example-bug discipline applied" — 3 example bugs caught and fixed BEFORE commit.** The 3 bugs (clox's `&&` vs `and`, 2-arg predicate rejected by arity check, clox's `>=` is numeric only) were all caught by **verifying the example output against the comments BEFORE commit** (15-occurrence pattern, the worst-class bug). The discipline: **always run the example through the clox binary before commit, and compare the output line-by-line to the comments.**
5. **"Slice operations family has 3 variants"** — Stage 51 (array_take), Stage 52 (array_drop), Stage 53 (array_take_while). The natural next step is `array_drop_while` (the complement) or `array_slice` (3-arg variant with start, end, step).
6. **"Type-predicate family is still COMPLETE"** — Stages 44-49 added is_array, is_string, is_number, is_bool, is_nil, is_function. The Stage 53 example doesn't use the type predicates directly, but `array_take_while` itself uses `IS_ARRAY` and `IS_CLOSURE` to validate inputs.
7. **"User-code-dispatch pattern is well-established"** — Stages 30 (filter), 31 (map), 32 (reduce), 33 (any), 34 (all), 40 (unique_by), 50 (zip_longest), 53 (take_while) all use `callClosureFromNative`. The architecture from Stage 30 handles every variation. The discipline: **when extending the architecture, read at least 1 prior implementation of the same pattern before writing the new one.** Stage 53 was modeled on Stage 33's `array_any` pattern.

## What this stage teaches

*(a) "Short-circuit slice pattern" — Stage 53 introduces a native that combines user-code-dispatch, short-circuit, and slice.* The pattern is "iterate from the start, call the predicate on each element, stop as soon as the predicate is falsy". The discipline: **handle the short-circuit correctly (pop the result array on the short-circuit return path to keep the GC stack balanced).**

*(b) "Stage 44's push/pop lesson was applied for the 9th consecutive stage" — 0 impl bugs in Stage 53.* The new `array_take_while` registration has 1 push and 1 pop, balanced. The function body has 1 push and 1 pop, balanced. The short-circuit return path also has 1 pop. The discipline: **count pushes and pops after every registration AND inside the function body, including every return path.** Stage 53 is 10th-consec-zero-bug in the new streak.

*(c) "Test-bug discipline applied" — 0 test bugs in this turn.* All 7 tests passed on the first run. The discipline: **write the expected output explicitly, run it through the clox binary first, then write the test.**

*(d) "Example-bug discipline applied" — 3 example bugs caught and fixed BEFORE commit.* The 3 bugs (clox's `&&` vs `and`, 2-arg predicate rejected by arity check, clox's `>=` is numeric only) were all caught by verifying the example output against the comments. The discipline: **always run the example through the clox binary before commit, and compare the output line-by-line to the comments.**

*(e) "Slice operations family has 3 variants."* Stage 51 (array_take), Stage 52 (array_drop), Stage 53 (array_take_while). The natural next step is `array_drop_while` (the complement). The discipline: **slice operations are a common idiom (pagination, top-N, head/tail, sub-array extraction, prefix/suffix, parse-until-marker).**

*(f) "Type-predicate family is still COMPLETE."* Stages 44-49 added is_array, is_string, is_number, is_bool, is_nil, is_function. The Stage 53 example doesn't use the type predicates directly, but `array_take_while` itself uses `IS_ARRAY` and `IS_CLOSURE` to validate inputs.

*(g) "User-code-dispatch pattern is well-established."* Stages 30 (filter), 31 (map), 32 (reduce), 33 (any), 34 (all), 40 (unique_by), 50 (zip_longest), 53 (take_while) all use `callClosureFromNative`. The architecture from Stage 30 handles every variation. The discipline: **when extending the architecture, read at least 1 prior implementation of the same pattern before writing the new one.** Stage 53 was modeled on Stage 33's `array_any` pattern.

## Limitations

- **`array_take_while` requires a 1-arg predicate** — predicates that need the array index or the previous element need to use outer-scope capture (stateful predicates). This is a clox limitation, not a Stage 53 limitation.
- **`array_take_while` doesn't have a `array_drop_while` variant** — only takes a prefix. Future stages may add `array_drop_while` (the complement) and `array_slice` (3-arg variant with start, end, step).
- **The new streak is at 10, still 6 short of the 16-streak record and 7 short of the 17-streak record.** Discipline: keep applying the push/pop lesson and the test-bug/example-bug disciplines.

## My pick for Stage 54

After Stage 53, the "slice operations" family has 3 variants (array_take, array_drop, array_take_while). The remaining candidates are:
- `array_drop_while(arr, predicate) -> array` — drop elements from the start while the predicate is truthy, return the rest. **My pick for Stage 54.** The natural complement of Stage 53's array_take_while. ~30 lines, low risk, reuses Stage 53's short-circuit + user-code-dispatch pattern.
- `array_slice(arr, start, end?, step?) -> array` — 3-arg variant with start, end, step. The "sub-array" idiom.
- `array_intersect(arr1, arr2) -> array` / `array_union(arr1, arr2) -> array` / `array_difference(arr1, arr2) -> array` — set operations.
- `string_pad_end(s, n, char?) -> string` — like Stage 22's string_pad_start but pads at the end.
- **Modules** — ~600 lines, biggest swing. Tom's call, not mine.
- **Apply Stages 1-53 to a different project** — the trigger-mine bucket grows beyond byox.

**My pick for Stage 54: `array_drop_while(arr, predicate) -> array`** — drop elements from the start while the predicate is truthy, return the rest. ~30 lines, low risk, reuses Stage 53's short-circuit + user-code-dispatch pattern. The new wrinkle: short-circuit drop (the complement of Stage 53's short-circuit take).

**After Stage 54, the next decision is one of:**
1. **`array_slice(arr, start, end?, step?)`** — 3-arg variant with start, end, step.
2. **`array_intersect` / `array_union` / `array_difference`** — set operations.
3. **`string_pad_end`** — like Stage 22's string_pad_start.
4. **Modules (~600 lines, Tom's call)** — the biggest swing.
5. **Apply Stages 1-53 to a different project** — the trigger-mine bucket grows beyond byox.

The next-pick for Stage 54 will be decided at the moment of decision, per the freeze-pattern antidote.

---

**File paths** (all confirmed):
- `05-vm/clox/src/native.c` — added `arrayTakeWhileNative` (~50 lines) + 12 lines registration after `array_drop`
- `05-vm/clox/tests/test_stdlib.c` — added 7 test functions + 7 main() invocations (165 lines)
- `05-vm/clox/examples/array-take-while.lox` — new example, 225 lines
- `05-vm/clox/docs/stage-53-closeout.md` — this file

**Branch:** `stage-53-array-take-while`, commit `2670faa` (impl + tests + example) + this file's commit.
