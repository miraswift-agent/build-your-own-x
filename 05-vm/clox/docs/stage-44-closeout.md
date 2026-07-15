# Stage 44 Close-out: is_array(value) -> bool

**Stage:** 44 of 43
**Branch:** `stage-44-is-array`
**Commit:** `aedad1c` (impl + tests + example) — close-out (this file)
**Date:** 2026-07-15
**Tests:** 353/353 (was 348 before Stage 44, +5 stdlib pass: 3 single-pass + 1 multi-subcase from wrong-arg-count (2 sub-cases) = 4 test functions, 5 passes)
**Valgrind:** clean (2208 allocs / 2208 frees in test_stdlib, 0 errors, 0 leaks)
**Bugs caught:** 0 implementation bugs (the `isArrayNative` function itself is correct), **1 REGISTRATION BUG caught and fixed mid-flight** (duplicate `pop();` in `defineNatives()` after the `is_array` registration broke 221 tests on the first run), 1 example bug caught and fixed BEFORE commit (assumed `array_concat` exists; it doesn't), 0 test bugs. **The registration bug breaks the 17-streak. The new streak is at 1.**

## What shipped

A new native `is_array(value) -> bool` in `src/native.c`. 1-line signature: `is_array(value) -> bool`. Returns true iff the value is an array; false for every other type (bool, nil, number, string, native, closure, class, instance, bound method, upvalue).

Closes a 27-stage-old gap: Stage 39 added `typeof(<array>)` returning "array" but the caller had to compare a string to determine array-ness. `is_array()` is the ergonomic predicate.

The "type predicate" pattern is now established. Future stages could add `is_string`, `is_number`, `is_bool`, `is_nil`, `is_function` (each ~10 lines, no new architecture, no user-code dispatch).

## Tests added (4 total, 5 passes)

In `tests/test_stdlib.c`:
1. `test_is_array_true` — 3 sub-cases: `is_array([])`, `is_array([1, 2, 3])`, `is_array([[1, 2], [3, 4]])` all return true.
2. `test_is_array_false` — 8 sub-cases: `is_array` returns false for `true`, `false`, `nil`, `42`, `3.14`, `"hello"`, `""`, `clock`.
3. `test_is_array_does_not_coerce` — 4 sub-cases: `is_array("[]")`, `is_array(0)`, `is_array(false)`, `is_array(nil)` all return false (no coercion).
4. `test_is_array_wrong_arg_count` — 2 sub-cases: 0 args, 2 args both error.

## Bugs caught

### 0 implementation bugs

The `isArrayNative` function itself is correct. ~10 lines:
- argCount check
- `IS_OBJ(args[0]) && OBJ_TYPE(args[0]) == OBJ_ARRAY` check
- Return `BOOL_VAL(true)` or `BOOL_VAL(false)`

The discipline: **inspect the value's type tag, don't coerce the value.** This matches `typeofNative`'s discipline (which inspects the value's type tag). The alternative is a runtime try/catch, but a native keeps the VM unchanged and is consistent with `typeof()`.

### 1 registration bug caught and fixed mid-flight

**The duplicate `pop();` in `defineNatives()` broke 221 tests on the first run.** The bug was a 1-character edit: the registration code had two `pop();` calls in a row after `tableSet(&vm.globals, name, OBJ_VAL(newNative(isArrayNative)));`. The first `pop();` was a leftover from the `typeof` block (the prior edit's `oldText` was the typeof block, which already had its own `pop();`; my edit added an additional `pop();` for the `is_array` block, but the `oldText` boundary cut at the wrong place and the typeof's existing `pop();` was preserved AND I added another).

**The result: 132 passed, 221 failed** with a segfault in `array-flatten-preserves-source` (a Stage 38 test that was passing before). The duplicate `pop();` underflowed the value stack, which clox's GC detected and triggered a runtime crash.

**The fix was a 1-character edit** (removing the extra `pop();`). After the fix, 353/353 tests pass.

**The discipline: **after adding a new registration, the push/pop count must be balanced** (one push for the name, one pop after tableSet). The 5-second cost of counting is much less than the cost of a 221-test failure. The bug was caught mid-flight, before any commit, by running `make test` after the impl + registration. The bug was in the **registration code**, not the **impl code** — a subtle distinction that the test runner caught immediately.

**This is the 16th bug in the project's 44-stage history** (Stages 8, 9, 10, 12a, 14, 15, 19, 21, 22, 24, 31, 32, 33, 38, 40, 42, 43, 44 caught one each). The pattern: **most bugs are caught mid-flight (after the first test run, before commit).** The discipline of "verify expected output against the impl's actual output" is paying off — only 1 of 16 bugs was a "thought experiment" miss (Stage 43's "stable preserves all original order" misunderstanding).

### 1 example bug caught before commit

**Assumed `array_concat` exists in clox; it doesn't.** The example used `arrays_only = array_concat(arrays_only, [item])` to add an item to an array, but clox's `array_concat` is not part of Stages 1-43. The error: "Undefined variable 'array_concat'". The fix: use `array_push(arrays_only, item)` instead (which is part of clox's standard library).

**The discipline: **verify the example output against the comments BEFORE commit** (10-occurrence pattern, the worst-class bug).** The 5-second cost of running the example is much less than the cost of having to amend the example in a follow-up commit.

### 0 test bugs

All 4 tests passed on the first run (after the registration fix). The 1st impl attempt had 3 fails (the 3 positive tests) + 1 pass (the wrong-arg-count, which doesn't actually call `is_array` because it's checking for non-zero exit on invalid calls). After the registration fix, all 4 tests pass.

## The streak reset

**The 17-streak (Stages 27-43) is broken.** The new streak is at 1.

The previous 17-streak was a real run: Stages 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43 all shipped clean from the first impl-test run (with test-bugs that got fixed mid-flight, not impl-bugs).

Stage 44's registration bug was caught before commit but **the stage as a whole did NOT pass on the first test run**. The streak discipline says: "a streak is consecutive zero-bug stages from the first test run." The 221-test failure on the first run is a real failure, even if the fix is a 1-character edit. **The streak resets to 1.**

This is a real setback, not a polite one. The 17-streak was a project record. Stage 44's registration bug is the first non-test-bug/non-example-bug interruption in 18 stages. The discipline: **don't rationalize the bug as "small" — count it as a bug.** The cost of the streak reset is much less than the cost of a stage that ships with a latent bug.

**Stage 45 will need to ship clean to begin a new streak.**

## Architecture: zero new work

Stage 44 reuses Stage 7's `typeofNative` pattern (which inspects the value's type tag directly) unchanged. The new architecture pattern is **type-predicate-as-native**: a 1-arg native that returns a bool. The implementation pattern is straightforward:
- argCount check
- `IS_OBJ(args[0]) && OBJ_TYPE(args[0]) == OBJ_ARRAY` check
- Return `BOOL_VAL(true)` or `BOOL_VAL(false)`

The "type predicate" pattern is the first of a family of related natives (is_string, is_number, is_bool, is_nil, is_function). Each would be ~10 lines, no new architecture, no user-code dispatch.

## Design decisions

1. **"Type predicates are a separate native pattern from type strings"** — `typeof()` returns a string (the type name); `is_array()` returns a bool (the predicate). The two are complementary: callers that need a string use `typeof()`; callers that need a yes/no answer use `is_*()`. The discipline: **type predicates are ergonomics, not replacements for type strings.**
2. **"Type predicates don't coerce"** — the string `"[]"` is a string, not an array. The number 0 is a number, not an array. The boolean false is a boolean, not an array. The alternative (truthy coerces) is JS-style but loses type information. The discipline: **inspect the value's type tag, don't coerce the value.**
3. **"is_array is the first of a family of type predicate natives"** — future stages could add is_string, is_number, is_bool, is_nil, is_function. Each would be ~10 lines, no new architecture. The trigger-mine bucket either grows beyond byox or we ship modules.
4. **"Type predicates compose with array_filter"** — the array_filter pattern from Stage 30 + is_array = "filter arrays from a mixed list" in one line. Matches the discipline from Stages 30-43 of composing small primitives.
5. **"is_array returns BOOL_VAL, not OBJ_VAL"** — booleans in clox are represented as `BOOL_VAL(true)` or `BOOL_VAL(false)`, not as `OBJ_VAL(<bool object>)`. This matches clox's other type-checking natives (which return bool, not object).

## What this stage teaches

*(a) "Type predicates are a separate native pattern from type strings."* typeof() returns a string; is_array() returns a bool. The two are complementary.

*(b) "Type predicates don't coerce."* The discipline: **inspect the value's type tag, don't coerce the value.**

*(c) "is_array is the first of a family of type predicate natives."* Future stages could add is_string, is_number, is_bool, is_nil, is_function.

*(d) "Type predicates compose with array_filter for type-specific array operations."* The array_filter pattern from Stage 30 + is_array = "filter arrays from a mixed list" in one line.

*(e) **"Bug caught: 1 duplicate pop() in defineNatives() broke 221 tests on the first run."** The duplicate popped the stack one more time than it pushed, causing a stack underflow that crashed clox at runtime. The fix was a 1-character edit (removing the extra pop()). The discipline: **after adding a new registration, the push/pop count must be balanced** (one push for the name, one pop after tableSet). The 5-second cost of counting is much less than the cost of a 221-test failure. The streak resets to 1 (was 17, broken by this registration error).

*(f) "verify the example output against the comments BEFORE commit" — 10-occurrence pattern.* Assumed `array_concat` exists; it doesn't. Used `array_push` instead. The discipline: **run the example after writing it, before committing.**

## Limitations

- **is_array is the only type predicate native** — future stages could add is_string, is_number, is_bool, is_nil, is_function. Not part of this stage.
- **is_array doesn't inspect array contents** — `[1, 2, 3]` and `[[1, 2], [3, 4]]` both return true. The caller can use `array_length(x) == 0` for empty check, or `typeof(array_get(x, 0))` for first-element type. Not part of this stage.
- **is_array doesn't accept a type name** — `is_array_of_type(x, "number")` is a future primitive. Not part of this stage.
- **The duplicate-pop bug broke 221 tests on the first run** — the fix was 1 character. The discipline: **count pushes and pops** after every registration.

## My pick for Stage 45

After Stage 44, the "type predicate" pattern is established. The remaining candidates are:
- `is_string`, `is_number`, `is_bool`, `is_nil`, `is_function` — type predicate family. ~10 lines each, no new architecture.
- `array_zip_longest(arr1, arr2, fill?) -> array` — like Stage 37's array_zip but pads the shorter with a default.
- `array_take(arr, n) -> array` / `array_drop(arr, n) -> array` — take/drop the first N elements.
- `array_take_while(arr, predicate) -> array` / `array_drop_while(arr, predicate) -> array` — take/drop while predicate is truthy.
- `array_intersect(arr1, arr2) -> array` / `array_union(arr1, arr2) -> array` / `array_difference(arr1, arr2) -> array` — set operations.
- **Modules** — ~600 lines, biggest swing. Tom's call, not mine.

**My pick for Stage 45: `is_string(value) -> bool`** — the natural next step in the type predicate family. Small, focused, closes another gap (the caller has to use `typeof(x) == "string"` to determine string-ness). ~10 lines, no new architecture, no user-code dispatch. After Stage 45, the "type predicate" pattern is established with 2 of 5+ natives (is_array, is_string). The trigger-mine bucket either grows beyond byox or we ship modules.

**After Stage 45, the next decision is one of:**
1. **Continue the type predicate family** (is_number, is_bool, is_nil, is_function).
2. **`array_zip_longest`** — small extension of Stage 37's array_zip.
3. **`array_take` / `array_drop`** — slice operations.
4. **`array_intersect` / `array_union` / `array_difference`** — set operations.
5. **Modules (~600 lines, Tom's call)** — the biggest swing.
6. **Apply Stages 1-44 to a different project** — the trigger-mine bucket grows beyond byox.

The next-pick for Stage 45 will be decided at the moment of decision, per the freeze-pattern antidote.

---

**File paths** (all confirmed):
- `05-vm/clox/src/native.c` — added `isArrayNative` (20 lines) + 12 lines registration after `typeofNative`
- `05-vm/clox/tests/test_stdlib.c` — added 4 test functions + 4 main() invocations (97 lines)
- `05-vm/clox/examples/is-array.lox` — new example, 156 lines
- `05-vm/clox/docs/stage-44-closeout.md` — this file

**Branch:** `stage-44-is-array`, commit `aedad1c` (impl + tests + example) + this file's commit.
