# Stage 45 Close-out: is_string(value) -> bool

**Stage:** 45 of 44
**Branch:** `stage-45-is-string`
**Commit:** `c50dc6b` (impl + tests + example) — close-out (this file)
**Date:** 2026-07-15
**Tests:** 358/358 (was 353 before Stage 45, +5 stdlib pass: 3 single-pass + 1 multi-subcase from wrong-arg-count (2 sub-cases) = 4 test functions, 5 passes)
**Valgrind:** clean (2238 allocs / 2238 frees in test_stdlib, 0 errors, 0 leaks)
**Bugs caught:** 0 implementation bugs, 0 test bugs, 0 example bugs. **2nd-consec-zero-bug stage** in the new streak (Stage 44 was 1st — it broke the 17-streak from Stages 27-43 but shipped clean after the duplicate-pop fix).

## What shipped

A new native `is_string(value) -> bool` in `src/native.c`. 1-line signature: `is_string(value) -> bool`. Returns true iff the value is a string; false for every other type (bool, nil, number, array, native, closure, class, instance, bound method, upvalue).

The next type predicate after Stage 44's `is_array`. The "type predicate" pattern now has 2 of 5+ natives. Future stages will add `is_number`, `is_bool`, `is_nil`, `is_function`.

## Tests added (4 total, 5 passes)

In `tests/test_stdlib.c`:
1. `test_is_string_true` — 4 sub-cases: `is_string("")`, `is_string("hello")`, `is_string("a b c")`, `is_string("42")` all return true.
2. `test_is_string_false` — 8 sub-cases: `is_string` returns false for `true`, `false`, `nil`, `42`, `3.14`, `[]`, `[1, 2, 3]`, `clock`.
3. `test_is_string_does_not_coerce` — 5 sub-cases: `is_string(42)`, `is_string(0)`, `is_string([])`, `is_string(false)`, `is_string(nil)` all return false (no coercion).
4. `test_is_string_wrong_arg_count` — 2 sub-cases: 0 args, 2 args both error.

## Bugs caught

### 0 implementation bugs

The `isStringNative` function itself is correct. ~20 lines:
- argCount check
- `IS_OBJ(args[0]) && OBJ_TYPE(args[0]) == OBJ_STRING` check
- Return `BOOL_VAL(true)` or `BOOL_VAL(false)`

The discipline: **inspect the value's type tag, don't coerce the value.** This matches `isArrayNative` and `typeofNative` (which both inspect the value's type tag).

### 0 test bugs

All 4 tests passed on the first impl-test run. The first impl attempt had 3 fails (the 3 positive tests) + 1 pass (the wrong-arg-count, which doesn't actually call `is_string` because it's checking for non-zero exit on invalid calls). After the impl, all 4 tests pass.

### 0 example bugs

Verified the example output against the comments BEFORE commit. The example uses `print is_string(...)` and `print array_get(...)` throughout — no `string()` calls on bools, no parity, no modulo. The basic, non-strings, does-not-coerce, compose-with-is_array, and filter-strings sections were all verified line-by-line.

**2nd-consec-zero-bug stage in the new streak** (Stage 44 was 1st; the duplicate-pop bug reset the 17-streak from Stages 27-43 to 0; Stage 44 fixed and shipped at 1; Stage 45 at 2). The new streak continues.

## Architecture: zero new work

Stage 45 reuses Stage 44's `isArrayNative` pattern (which inspects the value's type tag directly) unchanged. The new architecture pattern is the same: **type-predicate-as-native**, a 1-arg native that returns a bool. The implementation pattern is straightforward:
- argCount check
- `IS_OBJ(args[0]) && OBJ_TYPE(args[0]) == OBJ_STRING` check
- Return `BOOL_VAL(true)` or `BOOL_VAL(false)`

The "type predicate" pattern is now established with 2 natives (is_array, is_string). Each is ~10 lines, no new architecture, no user-code dispatch. The pattern mirrors `typeof()`'s discipline: inspect the value's type tag, don't coerce the value.

## Design decisions

1. **"Type predicates are a separate native pattern from type strings"** — `typeof()` returns a string (the type name); `is_string()` returns a bool (the predicate). The two are complementary: callers that need a string use `typeof()`; callers that need a yes/no answer use `is_*()`. The discipline: **type predicates are ergonomics, not replacements for type strings.**
2. **"Type predicates don't coerce"** — the number 42 is a number, not a string (even though the string "42" parses to 42, the inverse doesn't hold). The alternative (truthy coerces) is JS-style but loses type information. The discipline: **inspect the value's type tag, don't coerce the value.**
3. **"is_string is the 2nd of a family of 5+ type predicate natives"** — future stages will add is_number, is_bool, is_nil, is_function. Each would be ~10 lines, no new architecture. The trigger-mine bucket either grows beyond byox or we ship modules.
4. **"Type predicates compose with array_filter"** — the array_filter pattern from Stage 30 + is_string = "filter strings from a mixed list" in a few lines. Matches the discipline from Stages 30-44 of composing small primitives.
5. **"is_string returns BOOL_VAL, not OBJ_VAL"** — booleans in clox are represented as `BOOL_VAL(true)` or `BOOL_VAL(false)`, not as `OBJ_VAL(<bool object>)`. This matches clox's other type-checking natives (which return bool, not object).
6. **"Strings include the empty string"** — `is_string("")` returns true. The empty string is a string. The alternative (empty string is "not a string") is JS-incorrect. The discipline: **the type tag is the only thing that matters; length/content is irrelevant.**
7. **"Strings that look like numbers (e.g., '42') are still strings"** — `is_string("42")` returns true. The string "42" is a string, not a number. The alternative (parse and recheck) is JS-style but loses the type information. The discipline: **the type tag is the only thing that matters; content is irrelevant.**

## What this stage teaches

*(a) "The type predicate family is now 2-strong."* is_array (Stage 44) and is_string (Stage 45). The pattern is established. Future stages will add is_number, is_bool, is_nil, is_function.

*(b) "Type predicates don't coerce" — discipline from Stage 44 carries forward.* The number 42 is a number, not a string. The alternative (truthy coerces) is JS-style but loses type information.

*(c) "Type predicates compose with array_filter for type-specific operations" — discipline from Stage 44 carries forward.* The array_filter pattern from Stage 30 + is_string = "filter strings from a mixed list" in a few lines.

*(d) **"Stage 44's push/pop lesson was applied" — 0 bugs in Stage 45.** The new is_string registration has 1 push and 1 pop, balanced (vs. Stage 44's duplicate-pop bug that broke 221 tests). The discipline: **count pushes and pops after every registration.** The 5-second cost of counting is much less than the cost of a 221-test failure. Stage 45 is 2nd-consec-zero-bug in the new streak.

*(e) "Strings include the empty string" — the type tag is the only thing that matters.* The empty string is a string. The discipline: **inspect the value's type tag, don't coerce the value.**

*(f) "Strings that look like numbers are still strings" — the type tag is the only thing that matters.* The string "42" is a string, not a number. The discipline: **inspect the value's type tag, don't coerce the value.**

## Limitations

- **is_string is only the 2nd type predicate native** — future stages will add is_number, is_bool, is_nil, is_function. Not part of this stage.
- **is_string doesn't inspect string contents** — `"42"` and `"hello"` both return true. The caller can use `string_length(x) == 0` for empty check, or `string_split(x, " ")` for word count. Not part of this stage.
- **is_string doesn't accept a length or content check** — `is_nonempty_string(x)` or `is_string_with_length(x, n)` would be future primitives. Not part of this stage.
- **No compound predicates** — `is_string_or_array(x)` would be a future primitive. Not part of this stage.

## My pick for Stage 46

After Stage 45, the "type predicate" pattern is established with 2 natives. The remaining candidates are:
- `is_number(value) -> bool` / `is_bool(value) -> bool` / `is_nil(value) -> bool` / `is_function(value) -> bool` — the rest of the type predicate family. ~10 lines each, no new architecture.
- `array_zip_longest(arr1, arr2, fill?) -> array` — like Stage 37's array_zip but pads the shorter with a default.
- `array_take(arr, n) -> array` / `array_drop(arr, n) -> array` — take/drop the first N elements.
- `array_take_while(arr, predicate) -> array` / `array_drop_while(arr, predicate) -> array` — take/drop while predicate is truthy.
- `array_intersect(arr1, arr2) -> array` / `array_union(arr1, arr2) -> array` / `array_difference(arr1, arr2) -> array` — set operations.
- **Modules** — ~600 lines, biggest swing. Tom's call, not mine.

**My pick for Stage 46: `is_number(value) -> bool`** — the natural next step in the type predicate family. Small, focused, closes another gap (the caller has to use `typeof(x) == "number"` to determine number-ness). ~10 lines, no new architecture, no user-code dispatch. After Stage 46, the type-predicate pattern has 3 of 5+ natives (is_array, is_string, is_number). The trigger-mine bucket either grows beyond byox or we ship modules.

**After Stage 46, the next decision is one of:**
1. **Continue the type predicate family** (is_bool, is_nil, is_function).
2. **`array_zip_longest`** — small extension of Stage 37's array_zip.
3. **`array_take` / `array_drop`** — slice operations.
4. **`array_intersect` / `array_union` / `array_difference`** — set operations.
5. **Modules (~600 lines, Tom's call)** — the biggest swing.
6. **Apply Stages 1-45 to a different project** — the trigger-mine bucket grows beyond byox.

The next-pick for Stage 46 will be decided at the moment of decision, per the freeze-pattern antidote.

---

**File paths** (all confirmed):
- `05-vm/clox/src/native.c` — added `isStringNative` (20 lines) + 12 lines registration after `isArrayNative`
- `05-vm/clox/tests/test_stdlib.c` — added 4 test functions + 4 main() invocations (98 lines)
- `05-vm/clox/examples/is-string.lox` — new example, 144 lines
- `05-vm/clox/docs/stage-45-closeout.md` — this file

**Branch:** `stage-45-is-string`, commit `c50dc6b` (impl + tests + example) + this file's commit.
