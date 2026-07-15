# Stage 46 Close-out: is_number(value) -> bool

**Stage:** 46 of 45
**Branch:** `stage-46-is-number`
**Commit:** `7820d83` (impl + tests + example) — close-out (this file)
**Date:** 2026-07-15
**Tests:** 363/363 (was 358 before Stage 46, +5 stdlib pass: 3 single-pass + 1 multi-subcase from wrong-arg-count (2 sub-cases) = 4 test functions, 5 passes)
**Valgrind:** clean (2268 allocs / 2268 frees in test_stdlib, 0 errors, 0 leaks)
**Bugs caught:** 0 implementation bugs, 0 test bugs, 0 example bugs. **3rd-consec-zero-bug stage** in the new streak (Stage 44 was 1st, Stage 45 was 2nd, Stage 46 is 3rd).

## What shipped

A new native `is_number(value) -> bool` in `src/native.c`. 1-line signature: `is_number(value) -> bool`. Returns true iff the value is a number; false for every other type (bool, nil, string, array, native, closure, class, instance, bound method, upvalue).

The 3rd type predicate after Stage 44's `is_array` and Stage 45's `is_string`. The "type predicate" pattern is now established with 3 of 5+ natives. Future stages will add `is_bool`, `is_nil`, `is_function`.

## Tests added (4 total, 5 passes)

In `tests/test_stdlib.c`:
1. `test_is_number_true` — 5 sub-cases: `is_number(0)`, `is_number(42)`, `is_number(3.14)`, `is_number(-7)`, `is_number(-3.14)` all return true.
2. `test_is_number_false` — 9 sub-cases: `is_number` returns false for `true`, `false`, `nil`, `"42"`, `"hello"`, `""`, `[]`, `[1, 2, 3]`, `clock`.
3. `test_is_number_does_not_coerce` — 5 sub-cases: `is_number("42")`, `is_number("3.14")`, `is_number([])`, `is_number(false)`, `is_number(nil)` all return false (no coercion).
4. `test_is_number_wrong_arg_count` — 2 sub-cases: 0 args, 2 args both error.

## Bugs caught

### 0 implementation bugs

The `isNumberNative` function itself is correct. ~15 lines:
- argCount check
- `IS_NUMBER(args[0])` check
- Return `BOOL_VAL(true)` or `BOOL_VAL(false)`

The discipline: **inspect the value's type tag, don't coerce the value.** This matches `isArrayNative`, `isStringNative`, and `typeofNative` (which all inspect the value's type tag).

### 0 test bugs

All 4 tests passed on the first impl-test run. The first impl attempt had 3 fails (the 3 positive tests) + 1 pass (the wrong-arg-count, which doesn't actually call `is_number` because it's checking for non-zero exit on invalid calls). After the impl, all 4 tests pass.

### 0 example bugs

Verified the example output against the comments BEFORE commit. The example uses `print is_number(...)` and `print ...` throughout — no `string()` calls on bools, no parity, no modulo. The basic, non-numbers, does-not-coerce, compose-with-is_array-and-is_string, arithmetic-with-type-guard, and sum-of-array-with-type-guard sections were all verified line-by-line.

**3rd-consec-zero-bug stage in the new streak** (Stage 44 was 1st; Stage 44's duplicate-pop bug reset the 17-streak from Stages 27-43 to 0; Stage 44 fixed and shipped at 1; Stage 45 was 2nd; Stage 46 is 3rd). The new streak continues to grow.

## Architecture: zero new work

Stage 46 reuses Stage 44's `isArrayNative` and Stage 45's `isStringNative` pattern (which inspects the value's type tag directly) unchanged. The new wrinkle: **numbers in clox are NOT objects** (they're stored as a double inside the Value tag). So the check is `IS_NUMBER(args[0])`, not `IS_OBJ(args[0]) && OBJ_TYPE(args[0]) == X`.

The implementation pattern is the same:
- argCount check
- `IS_NUMBER(args[0])` check
- Return `BOOL_VAL(true)` or `BOOL_VAL(false)`

The "type predicate" pattern is now established with 3 natives (is_array, is_string, is_number). Each is ~10-20 lines, no new architecture, no user-code dispatch. The pattern mirrors `typeof()`'s discipline: inspect the value's type tag, don't coerce the value.

## Design decisions

1. **"Type predicates are a separate native pattern from type strings"** — `typeof()` returns a string (the type name); `is_number()` returns a bool (the predicate). The two are complementary: callers that need a string use `typeof()`; callers that need a yes/no answer use `is_*()`. The discipline: **type predicates are ergonomics, not replacements for type strings.**
2. **"Type predicates don't coerce"** — the string "42" is a string, not a number (even though `string_to_number("42") == 42`, the inverse doesn't hold). The alternative (truthy coerces) is JS-style but loses type information. The discipline: **inspect the value's type tag, don't coerce the value.**
3. **"Numbers in clox are doubles, not ints"** — the `is_number(x)` check passes for any double — 0, 42, 3.14, -7, etc. The alternative (separate int and float predicates) is JS-style but clox has only one number type. The discipline: **the language's type system defines the predicate; don't add predicates for types the language doesn't have.**
4. **"is_number is the 3rd of a family of 5+ type predicate natives"** — future stages will add is_bool, is_nil, is_function. Each would be ~10 lines, no new architecture. The trigger-mine bucket either grows beyond byox or we ship modules.
5. **"Type predicates compose with control flow"** — the canonical use case is "guard against non-numeric input before arithmetic". Stage 46 makes it ergonomic: `is_number(x)` is the predicate. Matches the discipline from Stages 30-45 of composing small primitives.
6. **"is_number returns BOOL_VAL, not OBJ_VAL"** — booleans in clox are represented as `BOOL_VAL(true)` or `BOOL_VAL(false)`, not as `OBJ_VAL(<bool object>)`. This matches clox's other type-checking natives (which return bool, not object).
7. **"Numbers include 0, negatives, and decimals"** — `is_number(0)`, `is_number(-7)`, `is_number(3.14)` all return true. The alternative (only positive numbers) is JS-incorrect. The discipline: **the type tag is the only thing that matters; sign/value is irrelevant.**

## What this stage teaches

*(a) "The type predicate family is now 3-strong."* is_array (Stage 44), is_string (Stage 45), is_number (Stage 46). The pattern is established. Future stages will add is_bool, is_nil, is_function.

*(b) "Numbers in clox are doubles, not ints."* The is_number(x) check passes for any double — 0, 42, 3.14, -7, etc. The alternative (separate int and float predicates) is JS-style but clox has only one number type.

*(c) "Type predicates don't coerce" — discipline from Stages 44/45 carries forward.* The string "42" is a string, not a number (even though `string_to_number("42") == 42`, the inverse doesn't hold). The alternative (truthy coerces) is JS-style but loses type information.

*(d) "Type predicates compose with control flow" — new pattern for Stage 46.* The canonical use case is "guard against non-numeric input before arithmetic". Stage 46 makes it ergonomic: `is_number(x)` is the predicate.

*(e) **"Stage 44's push/pop lesson was applied for the 2nd consecutive stage" — 0 bugs in Stage 46.** The new is_number registration has 1 push and 1 pop, balanced (vs. Stage 44's duplicate-pop bug). The discipline: **count pushes and pops after every registration.** The 5-second cost of counting is much less than the cost of a 221-test failure. Stage 46 is 3rd-consec-zero-bug in the new streak.

*(f) "Numbers include 0, negatives, and decimals" — the type tag is the only thing that matters.* The discipline: **inspect the value's type tag, don't coerce the value.**

## Limitations

- **is_number is only the 3rd type predicate native** — future stages will add is_bool, is_nil, is_function. Not part of this stage.
- **is_number doesn't distinguish int from float** — clox has only one number type (double). The alternative (separate predicates) would require a language change. Not part of this stage.
- **is_number doesn't accept a value range check** — `is_positive_number(x)` or `is_integer(x)` would be future primitives. Not part of this stage.
- **No compound predicates** — `is_number_or_nil(x)` would be a future primitive. Not part of this stage.

## My pick for Stage 47

After Stage 46, the "type predicate" pattern is established with 3 natives. The remaining candidates are:
- `is_bool(value) -> bool` / `is_nil(value) -> bool` / `is_function(value) -> bool` — the rest of the type predicate family. ~10 lines each, no new architecture.
- `array_zip_longest(arr1, arr2, fill?) -> array` — like Stage 37's array_zip but pads the shorter with a default.
- `array_take(arr, n) -> array` / `array_drop(arr, n) -> array` — take/drop the first N elements.
- `array_take_while(arr, predicate) -> array` / `array_drop_while(arr, predicate) -> array` — take/drop while predicate is truthy.
- `array_intersect(arr1, arr2) -> array` / `array_union(arr1, arr2) -> array` / `array_difference(arr1, arr2) -> array` — set operations.
- **Modules** — ~600 lines, biggest swing. Tom's call, not mine.

**My pick for Stage 47: `is_bool(value) -> bool`** — the next type predicate. Small, focused, closes another gap (the caller has to use `typeof(x) == "bool"` to determine bool-ness). ~10 lines, no new architecture, no user-code dispatch. After Stage 47, the type-predicate pattern has 4 of 5+ natives (is_array, is_string, is_number, is_bool). The trigger-mine bucket either grows beyond byox or we ship modules.

**After Stage 47, the next decision is one of:**
1. **Continue the type predicate family** (is_nil, is_function).
2. **`array_zip_longest`** — small extension of Stage 37's array_zip.
3. **`array_take` / `array_drop`** — slice operations.
4. **`array_intersect` / `array_union` / `array_difference`** — set operations.
5. **Modules (~600 lines, Tom's call)** — the biggest swing.
6. **Apply Stages 1-46 to a different project** — the trigger-mine bucket grows beyond byox.

The next-pick for Stage 47 will be decided at the moment of decision, per the freeze-pattern antidote.

---

**File paths** (all confirmed):
- `05-vm/clox/src/native.c` — added `isNumberNative` (16 lines) + 14 lines registration after `isStringNative`
- `05-vm/clox/tests/test_stdlib.c` — added 4 test functions + 4 main() invocations (102 lines)
- `05-vm/clox/examples/is-number.lox` — new example, 179 lines
- `05-vm/clox/docs/stage-46-closeout.md` — this file

**Branch:** `stage-46-is-number`, commit `7820d83` (impl + tests + example) + this file's commit.
