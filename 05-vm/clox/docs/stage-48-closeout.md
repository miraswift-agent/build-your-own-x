# Stage 48 Close-out: is_nil(value) -> bool

**Stage:** 48 of 47
**Branch:** `stage-48-is-nil`
**Commit:** `7edd1a3` (impl + tests + example) — close-out (this file)
**Date:** 2026-07-15
**Tests:** 373/373 (was 368 before Stage 48, +5 stdlib pass: 3 single-pass + 1 multi-subcase from wrong-arg-count (2 sub-cases) = 4 test functions, 5 passes)
**Valgrind:** clean (2328 allocs / 2328 frees in test_stdlib, 0 errors, 0 leaks)
**Bugs caught:** 0 implementation bugs, **1 test bug** (fixed mid-flight), **1 example bug** (fixed BEFORE commit). **5th-consec-zero-impl-bug stage in the new streak** (Stage 44 was 1st, Stage 45 was 2nd, Stage 46 was 3rd, Stage 47 was 4th, Stage 48 is 5th).

## What shipped

A new native `is_nil(value) -> bool` in `src/native.c`. 1-line signature: `is_nil(value) -> bool`. Returns true iff the value is nil; false for every other type (bool, number, string, array, native, closure, class, instance, bound method, upvalue).

The 5th type predicate after Stage 44's `is_array`, Stage 45's `is_string`, Stage 46's `is_number`, and Stage 47's `is_bool`. The "type predicate" pattern is now established with 5 of 5+ natives. Future stages may add `is_function`.

## Tests added (4 total, 5 passes)

In `tests/test_stdlib.c`:
1. `test_is_nil_true` — 4 sub-cases: `is_nil(nil)`, `is_nil(<var x = nil>)`, `is_nil(<uninitialized var>)`, `is_nil(<var z = nil>)` all return true.
2. `test_is_nil_false` — 11 sub-cases: `is_nil` returns false for `true`, `false`, `0`, `42`, `3.14`, `"nil"`, `"hello"`, `""`, `[]`, `[1, 2, 3]`, `clock`.
3. `test_is_nil_does_not_coerce` — 4 sub-cases: `is_nil(0)`, `is_nil("")`, `is_nil([])`, `is_nil(false)` all return false (no PHP-style nullish coercion).
4. `test_is_nil_wrong_arg_count` — 2 sub-cases: 0 args, 2 args both error.

## Bugs caught

### 0 implementation bugs

The `isNilNative` function itself is correct. ~19 lines:
- argCount check
- `IS_NIL(args[0])` check
- Return `BOOL_VAL(true)` or `BOOL_VAL(false)`

The discipline: **inspect the value's type tag, don't coerce the value.** This matches `isArrayNative`, `isStringNative`, `isNumberNative`, `isBoolNative`, and `typeofNative` (which all inspect the value's type tag).

### 1 test bug (fixed mid-flight)

The `test_is_nil_true` first draft included:
- `is_nil(1 == 2)` — expected true, got false
- `is_nil(!true)` — expected true, got false

**The test author (me) conflated "false" with "nil" (a Python/Lua-style mental model).** In Lox, `1 == 2` returns `false` (a boolean, NOT nil). `!true` returns `false` (a boolean, NOT nil). The test author assumed that "any value that compares equal to false" is nil — but clox's `false` is a bool, not nil.

The fix was a test edit, not a code change. The corrected test uses 4 distinct nil-producing cases: `is_nil(nil)`, `is_nil(<var x = nil>)`, `is_nil(<uninitialized var>)`, `is_nil(<var z = nil>)`. The discipline: **type predicates inspect the TYPE, not the value's truthiness.** This is the 16th test-bug in 48-stage history.

**The new lesson for Stage 48:** "false is not nil" is a clox-specific design. In Python, `False == None` is false but `0 == False` is true; in Lua, `false == nil` is false. The clox mental model is simpler: `false` is a bool, `nil` is nil, they are different types, `is_nil` returns false for `false`.

### 1 example bug (fixed BEFORE commit)

The `default_to` section's expected output had verbose parenthetical comments instead of the literal print output. The actual output is:
- `print default_to("", "default")` → empty line (the empty string)
- `print default_to([], [1, 2, 3])` → `[]` (the empty array)

My first draft had:
```
//   (empty string - default_to returns "" since it's not nil)
//   (empty array - default_to returns [] since it's not nil)
```
The fix was a 2-line edit to match the literal output. The discipline: **verify the example output against the comments BEFORE commit** (12-occurrence pattern, the worst-class bug — but caught early).

### Streak

**5th-consec-zero-impl-bug stage in the new streak** (Stage 44 was 1st; Stage 44's duplicate-pop bug reset the 17-streak from Stages 27-43 to 0; Stage 44 fixed and shipped at 1; Stage 45 was 2nd; Stage 46 was 3rd; Stage 47 was 4th; Stage 48 is 5th). The new streak continues to grow.

The test-bug doesn't break the impl-bug streak (the impl is correct, the test was wrong). The example-bug doesn't break the impl-bug streak either (the example is correct after the fix, the impl is correct).

## Architecture: zero new work

Stage 48 reuses Stages 44-47's type predicate pattern unchanged. The new wrinkle: **nil in clox is a dedicated Value tag (VAL_NIL)**, separate from numbers (VAL_NUMBER), booleans (VAL_BOOL), and objects (VAL_OBJ). The check is just `IS_NIL(args[0])` — no OBJ_TYPE dispatch needed.

The implementation pattern is the same as Stages 44-47:
- argCount check
- `IS_NIL(args[0])` check
- Return `BOOL_VAL(true)` or `BOOL_VAL(false)`

The "type predicate" pattern is now established with 5 natives (is_array, is_string, is_number, is_bool, is_nil). Each is ~10-20 lines, no new architecture, no user-code dispatch.

## Design decisions

1. **"Type predicates are a separate native pattern from type strings"** — `typeof()` returns a string (the type name); `is_nil()` returns a bool (the predicate). The two are complementary: callers that need a string use `typeof()`; callers that need a yes/no answer use `is_*()`. The discipline: **type predicates are ergonomics, not replacements for type strings.**
2. **"Type predicates don't coerce"** — the number 0 is NOT nil (even though some languages treat it as nullish). The empty string is NOT nil. The empty array is NOT nil. The boolean false is NOT nil. The alternative (PHP-style nullish coercion) loses type information. The discipline: **inspect the value's type tag, don't coerce the value.**
3. **"False is not nil"** — in clox, `false` is a bool, `nil` is nil, they are different types, `is_nil` returns false for `false`. This is a clox-specific design that matches the language's "only false and nil are falsy" rule (in terms of truthiness) but the TYPES are different.
4. **"Nil in clox is a dedicated Value tag"** — the is_nil(x) check is just `IS_NIL(args[0])`. The only way to get a nil value in clox is the `nil` literal or an uninitialized variable. The discipline: **the language's type system defines the predicate.**
5. **"is_nil is the 5th of a family of 5+ type predicate natives"** — future stages may add is_function. Each would be ~10 lines, no new architecture. The trigger-mine bucket either grows beyond byox or we ship modules.
6. **"Type predicates compose with control flow"** — the canonical use case is "default value when a variable is nil" or "filter nils from a mixed array". Stage 48 makes it ergonomic: `is_nil(x)` is the predicate. Matches the discipline from Stages 30-47 of composing small primitives.
7. **"is_nil returns BOOL_VAL, not OBJ_VAL"** — booleans in clox are represented as `BOOL_VAL(true)` or `BOOL_VAL(false)`, not as `OBJ_VAL(<bool object>)`. This matches clox's other type-checking natives.

## What this stage teaches

*(a) "The type predicate family is now 5-strong."* is_array (Stage 44), is_string (Stage 45), is_number (Stage 46), is_bool (Stage 47), is_nil (Stage 48). The pattern is established. Future stages may add is_function.

*(b) "Nil in clox is a dedicated Value tag."* The is_nil(x) check is `IS_NIL(args[0])`. The only way to get a nil value in clox is the `nil` literal or an uninitialized variable. Every comparison/!-operation is a bool, NOT nil.

*(c) "Type predicates don't coerce" — discipline from Stages 44-47 carries forward.* The number 0 is NOT nil. The empty array is NOT nil. The empty string is NOT nil. The boolean false is NOT nil. The alternative (PHP-style nullish coercion) loses type information.

*(d) "False is not nil" — new for Stage 48.* The confusion between "false" and "nil" is common in dynamic languages (Python, Lua). In clox, they are different types: false is a bool, nil is nil. The `is_nil` predicate makes this distinction explicit.

*(e) **"Stage 44's push/pop lesson was applied for the 4th consecutive stage" — 0 impl bugs in Stage 48.** The new is_nil registration has 1 push and 1 pop, balanced (vs. Stage 44's duplicate-pop bug). The discipline: **count pushes and pops after every registration.** The 5-second cost of counting is much less than the cost of a 221-test failure. Stage 48 is 5th-consec-zero-impl-bug in the new streak.

*(f) **"Test-bug caught: 16th test-bug in 48-stage history."** The first test of `test_is_nil_true` conflated "false" with "nil" (a Python/Lua-style mental model). The fix was a test edit, not a code change. The discipline: **type predicates inspect the TYPE, not the value's truthiness.**

*(g) **"Example-bug discipline applied" — 1 example bug caught and fixed BEFORE commit.** The default_to section's expected output had verbose parenthetical comments instead of the literal print output. The discipline: **verify the example output against the comments BEFORE commit** (12-occurrence pattern, the worst-class bug).

## Limitations

- **is_nil is the 5th type predicate native** — future stages may add is_function. Not part of this stage.
- **is_nil doesn't inspect the nil-ness of a function's return** — `is_nil(foo())` works, but the caller has to handle the case where `foo()` doesn't return a value (which is also nil in clox). The alternative (is_return_value(x)) would be a future primitive. Not part of this stage.
- **No compound predicates** — `is_nil_or_empty(x)` would be a future primitive. Not part of this stage.
- **The test-bug and example-bug were caught pre-commit** — but the patterns (16 test-bugs, 12 example-bugs) show they're frequent hazards. The discipline: always verify before commit.

## My pick for Stage 49

After Stage 48, the "type predicate" pattern is established with 5 natives. The remaining candidates are:
- `is_function(value) -> bool` — the last type predicate (5 of 5+). Small, focused, closes another gap (the caller has to use `typeof(x) == "function"` to determine function-ness). ~15 lines, no new architecture, no user-code dispatch. **The new wrinkle**: function-ness in clox has TWO sub-types: ObjClosure (Lox-defined) and ObjNative (C-defined). `is_function` would return true for both.
- `array_zip_longest(arr1, arr2, fill?) -> array` — like Stage 37's array_zip but pads the shorter with a default.
- `array_take(arr, n) -> array` / `array_drop(arr, n) -> array` — take/drop the first N elements.
- `array_intersect(arr1, arr2) -> array` / `array_union(arr1, arr2) -> array` / `array_difference(arr1, arr2) -> array` — set operations.
- **Modules** — ~600 lines, biggest swing. Tom's call, not mine.

**My pick for Stage 49: `is_function(value) -> bool`** — the last type predicate. Closes another gap (the caller has to use `typeof(x) == "function"` to determine function-ness). ~15 lines, no new architecture, no user-code dispatch. After Stage 49, the type-predicate pattern is COMPLETE (5 of 5+ natives — is_array, is_string, is_number, is_bool, is_nil, is_function). The trigger-mine bucket either grows beyond byox or we ship modules.

**After Stage 49, the next decision is one of:**
1. **`array_zip_longest`** — small extension of Stage 37's array_zip.
2. **`array_take` / `array_drop`** — slice operations.
3. **`array_intersect` / `array_union` / `array_difference`** — set operations.
4. **Modules (~600 lines, Tom's call)** — the biggest swing.
5. **Apply Stages 1-48 to a different project** — the trigger-mine bucket grows beyond byox.

The next-pick for Stage 49 will be decided at the moment of decision, per the freeze-pattern antidote.

---

**File paths** (all confirmed):
- `05-vm/clox/src/native.c` — added `isNilNative` (19 lines) + 14 lines registration after `isBoolNative`
- `05-vm/clox/tests/test_stdlib.c` — added 4 test functions + 4 main() invocations (110 lines)
- `05-vm/clox/examples/is-nil.lox` — new example, 199 lines
- `05-vm/clox/docs/stage-48-closeout.md` — this file

**Branch:** `stage-48-is-nil`, commit `7edd1a3` (impl + tests + example) + this file's commit.
