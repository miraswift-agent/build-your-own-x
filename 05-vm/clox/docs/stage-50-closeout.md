# Stage 50 Close-out: array_zip_longest(arr1, arr2, fill?) -> array

**Stage:** 50 of 49
**Branch:** `stage-50-array-zip-longest`
**Commit:** `74d53f3` (impl + tests + example) — close-out (this file)
**Date:** 2026-07-15
**Tests:** 385/385 (was 378 before Stage 50, +7 stdlib pass: 3 positive single-pass + 1 multi-subcase wrong-arg-count + empty (3 sub-cases) = 4 functions, 7 passes total)
**Valgrind:** clean (2400 allocs / 2400 frees in test_stdlib, 0 errors, 0 leaks)
**Bugs caught:** 0 implementation bugs, **2 test bugs** (fixed mid-flight), **1 example bug** (fixed BEFORE commit). **7th-consec-zero-impl-bug stage in the new streak** (Stage 44 was 1st, Stage 45 was 2nd, Stage 46 was 3rd, Stage 47 was 4th, Stage 48 was 5th, Stage 49 was 6th, Stage 50 is 7th).

## What shipped

A new native `array_zip_longest(arr1, arr2, fill?) -> array` in `src/native.c`. 2 or 3 arguments (the optional 3rd is the fill value; defaults to nil when absent). Iterates up to `max(arr1->count, arr2->count)`, pairing elements from both arrays and using the fill where one is exhausted.

The first clox native with an **optional positional parameter**. clox's native API uses `(int argCount, Value *args)` — the optional pattern is `if (argCount > 2) use args[2] else use NIL_VAL`. The discipline: **handle the optional arg correctly.**

## Tests added (5 functions, 7 sub-cases)

In `tests/test_stdlib.c`:
1. `test_array_zip_longest_basic` — 4 sub-cases: `array_zip_longest([1, 2, 3], ["a", "b"])` returns `[[1, "a"], [2, "b"], [3, nil]]` (3rd element filled with nil).
2. `test_array_zip_longest_with_fill` — 4 sub-cases: `array_zip_longest([1, 2, 3, 4], [10, 20], 0)` returns `[[1, 10], [2, 20], [3, 0], [4, 0]]` (3rd and 4th elements filled with 0).
3. `test_array_zip_longest_equal_length` — 3 sub-cases: `array_zip_longest([1, 2, 3], ["a", "b", "c"], "X")` returns `[[1, "a"], [2, "b"], [3, "c"]]` (no fill needed).
4. `test_array_zip_longest_empty` — 3 sub-cases: `array_zip_longest([1, 2, 3], [], 0)` returns `[[1, 0], [2, 0], [3, 0]]`; `array_zip_longest([], [], 0)` returns `[]`; `array_zip_longest([], [1, 2], 0)` returns `[[0, 1], [0, 2]]`.
5. `test_array_zip_longest_wrong_arg_count` — 2 sub-cases: 1 arg, 4 args both error.

## Bugs caught

### 0 implementation bugs

The `arrayZipLongestNative` function itself is correct. ~50 lines:
- argCount check (must be 2 or 3)
- Type checks (arr1, arr2 must be arrays)
- Optional 3rd arg (the fill value)
- Outer loop up to `max(arr1->count, arr2->count)`
- Inner loop builds a 2-element pair, pushing the appropriate value or fill
- Result array pushed BEFORE the inner loop, popped AFTER (GC safety)

The discipline: **inspect the inputs, build the result, return.** No new architecture, no user-code dispatch.

### 2 test bugs (fixed mid-flight)

**Test bug #1: variable-name typo.** The empty test had `out1` and `out2` (one per sub-case), but the `fail()` calls in the first sub-case referenced `out` (no number). Compile error: `'out' undeclared (first use in this function); did you mean 'out1'?`. The fix was a test edit (change `out` to `out1` in two places).

**Test bug #2: clox's `string()` is number-to-string only.** The basic test used `string(array_get(array_get(zipped, i), 0))` to convert the pair's first element to a string. But the first element when a2 is exhausted is `NIL_VAL`, not a number, and `string(nil)` errors with `Error: string() argument must be a number.` (Stage 17 lesson: clox's `string()` is number-to-string only). The fix was to add a helper `s(v)` that handles strings, nils, and numbers via the type predicates from Stages 44-48.

**The discipline:** **use type predicates (is_string, is_nil, is_number) to dispatch on type, then use `string()` only on numbers.** This is the canonical use case for the 6 type-predicate natives.

### 1 example bug (fixed BEFORE commit)

The `pad a table to uniform rows` section's `s()` helper didn't handle arrays (`zipped_rows2` contained arrays as the first element). The fix was to add `is_array` to the helper. The discipline: **verify the example output against the comments BEFORE commit** (14-occurrence pattern, the worst-class bug).

### Streak

**7th-consec-zero-impl-bug stage in the new streak** (Stage 44 was 1st; Stage 44's duplicate-pop bug reset the 17-streak from Stages 27-43 to 0; Stage 44 fixed and shipped at 1; Stage 45 was 2nd; Stage 46 was 3rd; Stage 47 was 4th; Stage 48 was 5th; Stage 49 was 6th; Stage 50 is 7th). The new streak continues to grow.

The test-bugs and example-bug don't break the impl-bug streak (the impl is correct, the tests/example were wrong).

## Architecture: zero new work

Stage 50 reuses Stage 37's `array_zip` pattern. The new wrinkle: **first time a clox native has an optional positional parameter.** clox's native API uses `(int argCount, Value *args)` — the optional pattern is `if (argCount > 2) use args[2] else use NIL_VAL`.

The implementation pattern is similar to Stage 37 but with a multi-iteration loop and an inner pair array:
- argCount check (must be 2 or 3)
- Type checks (arr1, arr2 must be arrays)
- Optional 3rd arg (the fill value)
- Outer loop up to `max(arr1->count, arr2->count)`
- Inner loop builds a 2-element pair, pushing the appropriate value or fill
- Result array pushed BEFORE the inner loop, popped AFTER (GC safety)

The "type predicate" family is now COMPLETE (6 of 5+ natives) — Stage 50's example uses `is_string` and `is_nil` to pretty-print a value. **The type-predicate pattern enables ergonomic type-aware code.**

## Design decisions

1. **"First clox native with an optional positional parameter"** — `array_zip_longest` takes 2 or 3 args. The 3rd arg is the fill value; defaults to nil when absent. The discipline: **handle the optional arg correctly (if argCount > 2, use args[2]; else use NIL_VAL as the default).** This pattern is reusable for future natives (e.g., `array_repeat(arr, n, separator?)`, `array_join(arr, separator?)`, `string_pad_start(s, n, char?)`).
2. **"Stage 44's push/pop lesson was applied for the 6th consecutive stage" — 0 impl bugs in Stage 50.** The new `array_zip_longest` registration has 1 push and 1 pop, balanced (vs. Stage 44's duplicate-pop bug). The inner loop also balances (pair push/pop per iteration, result push/pop once). The discipline: **count pushes and pops after every registration AND every iteration of a loop that uses push/pop.**
3. **"Test-bug caught: 18th-19th test-bugs in 50-stage history."** The first test had a variable-name typo (`out` instead of `out1`/`out2`) and the second test had a `string()` only-takes-numbers miss. The fix was a test edit. The discipline: **clox's `string()` is number-to-string only** (Stage 17 lesson); for booleans/strings/nils, use a separate helper or print statements.
4. **"Example-bug discipline applied" — 1 example bug caught and fixed BEFORE commit.** The `s()` helper didn't handle arrays. The fix was to add `is_array` to the helper. The discipline: **verify the example output against the comments BEFORE commit** (14-occurrence pattern, the worst-class bug).
5. **"Type-predicate family is now COMPLETE"** — Stages 44-49 added is_array, is_string, is_number, is_bool, is_nil, is_function. The Stage 50 example uses is_string and is_nil to pretty-print a value. **The type-predicate pattern enables ergonomic type-aware code.**
6. **"Parallel arrays GC pattern"** — Stage 42 lesson. The result array is pushed BEFORE the inner loop, so any allocation in the inner loop (e.g., the pair array) doesn't sweep away the result. The pair array is pushed and popped within each iteration, keeping the GC accounting balanced.
7. **"Optional 3rd arg pattern"** — for the first time, a clox native has an optional positional parameter. The pattern is reusable for future natives. The discipline: **handle the optional arg correctly; use NIL_VAL as the default.**

## What this stage teaches

*(a) "First clox native with an optional positional parameter."* `array_zip_longest` takes 2 or 3 args. The 3rd arg is the fill value; defaults to nil when absent. The discipline: **handle the optional arg correctly.**

*(b) "Stage 44's push/pop lesson was applied for the 6th consecutive stage" — 0 impl bugs in Stage 50.* The new `array_zip_longest` registration has 1 push and 1 pop, balanced. The inner loop also balances. The discipline: **count pushes and pops after every registration AND every iteration of a loop that uses push/pop.**

*(c) **"Test-bug caught: 18th-19th test-bugs in 50-stage history."** The first test had a variable-name typo and the second test had a `string()` only-takes-numbers miss. The fix was a test edit. The discipline: **clox's `string()` is number-to-string only.** *

*(d) **"Example-bug discipline applied" — 1 example bug caught and fixed BEFORE commit.** The `s()` helper didn't handle arrays. The fix was to add `is_array` to the helper. The discipline: **verify the example output against the comments BEFORE commit.** *

*(e) "Type-predicate family is now COMPLETE."* Stages 44-49 added is_array, is_string, is_number, is_bool, is_nil, is_function. The Stage 50 example uses is_string and is_nil to pretty-print a value. **The type-predicate pattern enables ergonomic type-aware code.**

*(f) "Parallel arrays GC pattern" — Stage 42 lesson.* The result array is pushed BEFORE the inner loop, so any allocation in the inner loop doesn't sweep away the result. The pair array is pushed and popped within each iteration.

*(g) "Optional 3rd arg pattern" — reusable for future natives.* The discipline: **handle the optional arg correctly; use NIL_VAL as the default.**

## Limitations

- **`array_zip_longest` doesn't accept a 3-arg function variant** — like `array_zip`, the result is always 2-element pairs. A `array_zip_longest_with(arr1, arr2, fill, combiner)` variant is a future stage.
- **`array_zip_longest` doesn't accept variadic arrays** — takes exactly 2 arrays. A `array_zip_longest_n(arr1, arr2, ..., arrN, fill?)` variant is a future stage.
- **`array_zip_longest` doesn't accept a "truncate" mode** — always pads. The opposite mode (truncate to the shorter) is `array_zip` (Stage 37).
- **The test-bugs and example-bug were caught pre-commit** — but the patterns (18-19 test-bugs, 14 example-bugs) show they're frequent hazards. The discipline: always verify before commit.

## My pick for Stage 51

After Stage 50, the "type predicate" pattern is COMPLETE, and the "array zip" family has 2 variants (zip, zip_longest). The remaining candidates are:
- `array_take(arr, n) -> array` / `array_drop(arr, n) -> array` — take/drop the first N elements.
- `array_take_while(arr, predicate) -> array` / `array_drop_while(arr, predicate) -> array` — take/drop while predicate is truthy.
- `array_intersect(arr1, arr2) -> array` / `array_union(arr1, arr2) -> array` / `array_difference(arr1, arr2) -> array` — set operations.
- `string_pad_end(s, n, char?) -> string` — like Stage 22's string_pad_start but pads at the end.
- **Modules** — ~600 lines, biggest swing. Tom's call, not mine.
- **Apply Stages 1-50 to a different project** — the trigger-mine bucket grows beyond byox.

**My pick for Stage 51: `array_take(arr, n) -> array`** — take the first N elements of an array. ~25 lines, low risk, reuses the array-building pattern from Stage 32's `array_filter` and Stage 50's `array_zip_longest`. The new wrinkle: a "size limit" pattern, similar to Stage 41's `array_chunk`.

**After Stage 51, the next decision is one of:**
1. **`array_drop(arr, n)`** — drop the first N elements (the complement of array_take).
2. **`array_take_while` / `array_drop_while`** — short-circuit slice operations.
3. **`array_intersect` / `array_union` / `array_difference`** — set operations.
4. **Modules (~600 lines, Tom's call)** — the biggest swing.
5. **Apply Stages 1-50 to a different project** — the trigger-mine bucket grows beyond byox.

The next-pick for Stage 51 will be decided at the moment of decision, per the freeze-pattern antidote.

---

**File paths** (all confirmed):
- `05-vm/clox/src/native.c` — added `arrayZipLongestNative` (~50 lines) + 12 lines registration after `array_zip`
- `05-vm/clox/tests/test_stdlib.c` — added 5 test functions + 5 main() invocations (136 lines)
- `05-vm/clox/examples/array-zip-longest.lox` — new example, 203 lines
- `05-vm/clox/docs/stage-50-closeout.md` — this file

**Branch:** `stage-50-array-zip-longest`, commit `74d53f3` (impl + tests + example) + this file's commit.
