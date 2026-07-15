# Stage 47 Close-out: is_bool(value) -> bool

**Stage:** 47 of 46
**Branch:** `stage-47-is-bool`
**Commit:** `035f184` (impl + tests + example) — close-out (this file)
**Date:** 2026-07-15
**Tests:** 368/368 (was 363 before Stage 47, +5 stdlib pass: 3 single-pass + 1 multi-subcase from wrong-arg-count (2 sub-cases) = 4 test functions, 5 passes)
**Valgrind:** clean (2298 allocs / 2298 frees in test_stdlib, 0 errors, 0 leaks)
**Bugs caught:** 0 implementation bugs, 0 test bugs, **1 example bug** (fixed BEFORE commit). **4th-consec-zero-bug stage in the new streak** for impl + test bugs (Stage 44 was 1st, Stage 45 was 2nd, Stage 46 was 3rd, Stage 47 is 4th).

## What shipped

A new native `is_bool(value) -> bool` in `src/native.c`. 1-line signature: `is_bool(value) -> bool`. Returns true iff the value is a boolean; false for every other type (nil, number, string, array, native, closure, class, instance, bound method, upvalue).

The 4th type predicate after Stage 44's `is_array`, Stage 45's `is_string`, and Stage 46's `is_number`. The "type predicate" pattern is now established with 4 of 5+ natives. Future stages will add `is_nil`, `is_function`.

## Tests added (4 total, 5 passes)

In `tests/test_stdlib.c`:
1. `test_is_bool_true` — 5 sub-cases: `is_bool(true)`, `is_bool(false)`, `is_bool(1 == 1)`, `is_bool(1 == 2)`, `is_bool(!nil)` all return true.
2. `test_is_bool_false` — 10 sub-cases: `is_bool` returns false for `nil`, `0`, `42`, `3.14`, `"true"`, `"hello"`, `""`, `[]`, `[true, false]`, `clock`.
3. `test_is_bool_does_not_coerce` — 5 sub-cases: `is_bool(0)`, `is_bool(1)`, `is_bool("")`, `is_bool([])`, `is_bool(nil)` all return false (no JS-style falsy coercion).
4. `test_is_bool_wrong_arg_count` — 2 sub-cases: 0 args, 2 args both error.

## Bugs caught

### 0 implementation bugs

The `isBoolNative` function itself is correct. ~17 lines:
- argCount check
- `IS_BOOL(args[0])` check
- Return `BOOL_VAL(true)` or `BOOL_VAL(false)`

The discipline: **inspect the value's type tag, don't coerce the value.** This matches `isArrayNative`, `isStringNative`, `isNumberNative`, and `typeofNative` (which all inspect the value's type tag).

### 0 test bugs

All 4 tests passed on the first impl-test run. The first impl attempt had 3 fails (the 3 positive tests) + 1 pass (the wrong-arg-count). After the impl, all 4 tests pass.

### 1 example bug (fixed BEFORE commit)

The `filter_bools` section's expected output was wrong. The comment said:
```
// Output:
//   true
//   false
//   (empty - filter of [1, 2, 3] returns nothing)
//   (empty - filter of [] returns nothing)
```
But the actual output is `print` of an array, which prints `[true, false]` on ONE line, not `true` and `false` on two lines. The fix was a 2-line edit in the example comment. The discipline: **verify the example output against the comments BEFORE commit** (11-occurrence pattern, the worst-class bug — but caught early).

**4th-consec-zero-bug stage in the new streak for impl + test bugs** (Stage 44 was 1st; Stage 44's duplicate-pop bug reset the 17-streak from Stages 27-43 to 0; Stage 44 fixed and shipped at 1; Stage 45 was 2nd; Stage 46 was 3rd; Stage 47 is 4th). The new streak continues to grow.

## Architecture: zero new work

Stage 47 reuses Stages 44/45/46's type predicate pattern unchanged. The new wrinkle: **booleans in clox are a dedicated Value tag (VAL_BOOL)**, separate from the numbers (VAL_NUMBER) and objects (VAL_OBJ). The check is just `IS_BOOL(args[0])` — no OBJ_TYPE dispatch needed.

The implementation pattern is the same as Stages 44/45/46:
- argCount check
- `IS_BOOL(args[0])` check
- Return `BOOL_VAL(true)` or `BOOL_VAL(false)`

The "type predicate" pattern is now established with 4 natives (is_array, is_string, is_number, is_bool). Each is ~10-20 lines, no new architecture, no user-code dispatch.

## Design decisions

1. **"Type predicates are a separate native pattern from type strings"** — `typeof()` returns a string (the type name); `is_bool()` returns a bool (the predicate). The two are complementary: callers that need a string use `typeof()`; callers that need a yes/no answer use `is_*()`. The discipline: **type predicates are ergonomics, not replacements for type strings.**
2. **"Type predicates don't coerce"** — the number 0 is NOT false (even though C and Python treat it as falsy). The number 1 is NOT true. The empty array is NOT false (even though JS treats it as truthy). The empty string is NOT false. The alternative (JS-style falsy coercion) loses type information. The discipline: **inspect the value's type tag, don't coerce the value.**
3. **"Booleans in clox are a dedicated Value tag"** — the is_bool(x) check is just `IS_BOOL(args[0])`. The alternative is to check for == true or == false, but that doesn't tell you the TYPE of the value. The discipline: **the language's type system defines the predicate.**
4. **"is_bool is the 4th of a family of 5+ type predicate natives"** — future stages will add is_nil, is_function. Each would be ~10 lines, no new architecture. The trigger-mine bucket either grows beyond byox or we ship modules.
5. **"Type predicates compose with control flow"** — the canonical use case is "guard against non-bool input before a boolean operation". Stage 47 makes it ergonomic: `is_bool(x)` is the predicate. Matches the discipline from Stages 30-46 of composing small primitives.
6. **"is_bool returns BOOL_VAL, not OBJ_VAL"** — booleans in clox are represented as `BOOL_VAL(true)` or `BOOL_VAL(false)`, not as `OBJ_VAL(<bool object>)`. This matches clox's other type-checking natives.
7. **"Comparisons and !-operations return booleans"** — `1 == 1` is a bool, `1 == 2` is a bool, `!nil` is a bool. So `is_bool(1 == 1) == true`. The test covers this. The discipline: **every comparison/!-operation is a bool, so is_bool returns true for them.**

## What this stage teaches

*(a) "The type predicate family is now 4-strong."* is_array (Stage 44), is_string (Stage 45), is_number (Stage 46), is_bool (Stage 47). The pattern is established. Future stages will add is_nil, is_function.

*(b) "Booleans in clox are a dedicated Value tag."* The is_bool(x) check is `IS_BOOL(args[0])`. The alternative is to check for == true or == false, but that doesn't tell you the TYPE of the value. Every comparison/!-operation is a bool, so is_bool returns true for them.

*(c) "Type predicates don't coerce" — discipline from Stages 44/45/46 carries forward.* The number 0 is NOT false. The empty array is NOT false. The empty string is NOT false. The alternative (JS-style falsy coercion) loses type information.

*(d) "Type predicates compose with control flow" — new pattern for Stage 47.* The canonical use case is "guard against non-bool input before a boolean operation". Stage 47 makes it ergonomic: `is_bool(x)` is the predicate.

*(e) **"Stage 44's push/pop lesson was applied for the 3rd consecutive stage" — 0 impl + test bugs in Stage 47.** The new is_bool registration has 1 push and 1 pop, balanced (vs. Stage 44's duplicate-pop bug). The discipline: **count pushes and pops after every registration.** The 5-second cost of counting is much less than the cost of a 221-test failure. Stage 47 is 4th-consec-zero-bug in the new streak (impl + test bugs).

*(f) **"Example-bug discipline applied" — 1 example bug caught and fixed BEFORE commit.** The filter_bools section's expected output was wrong. The discipline: **verify the example output against the comments BEFORE commit** (11-occurrence pattern, the worst-class bug). The fix was a 2-line edit; the example now runs cleanly.

## Limitations

- **is_bool is only the 4th type predicate native** — future stages will add is_nil, is_function. Not part of this stage.
- **is_bool doesn't inspect the boolean value** — `is_bool(true)` and `is_bool(false)` are both true. The alternative (is_true / is_false) would be future primitives. Not part of this stage.
- **No compound predicates** — `is_bool_or_nil(x)` would be a future primitive. Not part of this stage.
- **The example bug was caught pre-commit** — but the example-bug pattern (11-occurrence) shows it's a frequent hazard. The discipline: always verify output against comments before commit.

## My pick for Stage 48

After Stage 47, the "type predicate" pattern is established with 4 natives. The remaining candidates are:
- `is_nil(value) -> bool` — the next type predicate. Small, focused, closes another gap (the caller has to use `typeof(x) == "nil"` to determine nil-ness).
- `is_function(value) -> bool` — the last type predicate (5 of 5+).
- `array_zip_longest(arr1, arr2, fill?) -> array` — like Stage 37's array_zip but pads the shorter with a default.
- `array_take(arr, n) -> array` / `array_drop(arr, n) -> array` — take/drop the first N elements.
- `array_intersect(arr1, arr2) -> array` / `array_union(arr1, arr2) -> array` / `array_difference(arr1, arr2) -> array` — set operations.
- **Modules** — ~600 lines, biggest swing. Tom's call, not mine.

**My pick for Stage 48: `is_nil(value) -> bool`** — the next type predicate. Small, focused, closes another gap (the caller has to use `typeof(x) == "nil"` to determine nil-ness). ~10 lines, no new architecture, no user-code dispatch. After Stage 48, the type-predicate pattern has 5 of 5+ natives. The trigger-mine bucket either grows beyond byox or we ship modules.

**After Stage 48, the next decision is one of:**
1. **Continue the type predicate family** (is_function — the last one).
2. **`array_zip_longest`** — small extension of Stage 37's array_zip.
3. **`array_take` / `array_drop`** — slice operations.
4. **`array_intersect` / `array_union` / `array_difference`** — set operations.
5. **Modules (~600 lines, Tom's call)** — the biggest swing.
6. **Apply Stages 1-47 to a different project** — the trigger-mine bucket grows beyond byox.

The next-pick for Stage 48 will be decided at the moment of decision, per the freeze-pattern antidote.

---

**File paths** (all confirmed):
- `05-vm/clox/src/native.c` — added `isBoolNative` (19 lines) + 14 lines registration after `isNumberNative`
- `05-vm/clox/tests/test_stdlib.c` — added 4 test functions + 4 main() invocations (103 lines)
- `05-vm/clox/examples/is-bool.lox` — new example, 184 lines
- `05-vm/clox/docs/stage-47-closeout.md` — this file

**Branch:** `stage-47-is-bool`, commit `035f184` (impl + tests + example) + this file's commit.
