# Stage 49 Close-out: is_function(value) -> bool

**Stage:** 49 of 48
**Branch:** `stage-49-is-function`
**Commit:** `d662566` (impl + tests + example) — close-out (this file)
**Date:** 2026-07-15
**Tests:** 378/378 (was 373 before Stage 49, +5 stdlib pass: 3 single-pass + 1 multi-subcase from wrong-arg-count (2 sub-cases) = 4 test functions, 5 passes)
**Valgrind:** clean (2358 allocs / 2358 frees in test_stdlib, 0 errors, 0 leaks)
**Bugs caught:** 0 implementation bugs, **1 test bug** (fixed mid-flight), **1 example bug** (fixed BEFORE commit). **6th-consec-zero-impl-bug stage in the new streak** (Stage 44 was 1st, Stage 45 was 2nd, Stage 46 was 3rd, Stage 47 was 4th, Stage 48 was 5th, Stage 49 is 6th).

## What shipped

A new native `is_function(value) -> bool` in `src/native.c`. 1-line signature: `is_function(value) -> bool`. Returns true iff the value is a function (Lox closure OR C-defined native); false for every other type (bool, nil, number, string, array, class, instance, bound method, upvalue).

The 6th and FINAL type predicate after Stage 44's `is_array`, Stage 45's `is_string`, Stage 46's `is_number`, Stage 47's `is_bool`, and Stage 48's `is_nil`. **The "type predicate" pattern is now COMPLETE (5 of 5+ natives).** After Stage 49, the caller has a complete type-checking toolbox.

## Tests added (4 total, 5 passes)

In `tests/test_stdlib.c`:
1. `test_is_function_true` — 5 sub-cases: `is_function(noop)` (Lox closure), `is_function(inc)` (Lox closure with 1 arg), `is_function(clock)` (native), `is_function(string)` (native), `is_function(array_length)` (native) all return true.
2. `test_is_function_false` — 11 sub-cases: `is_function` returns false for `true`, `false`, `nil`, `42`, `3.14`, `"clock"`, `"hello"`, `""`, `[]`, `[1, 2, 3]`, `[clock]`.
3. `test_is_function_does_not_coerce` — 5 sub-cases: `is_function("clock")`, `is_function("fun")`, `is_function([clock])`, `is_function(0)`, `is_function(false)` all return false (no loose coercion).
4. `test_is_function_wrong_arg_count` — 2 sub-cases: 0 args, 2 args both error.

## Bugs caught

### 0 implementation bugs

The `isFunctionNative` function itself is correct. ~32 lines:
- argCount check
- `IS_OBJ(args[0])` check
- `OBJ_TYPE(args[0]) == OBJ_CLOSURE || OBJ_TYPE(args[0]) == OBJ_NATIVE` check
- Return `BOOL_VAL(true)` or `BOOL_VAL(false)`

The discipline: **inspect the value's type tag, don't coerce the value.** This matches `isArrayNative`, `isStringNative`, `isNumberNative`, `isBoolNative`, `isNilNative`, and `typeofNative` (which all inspect the value's type tag).

### 1 test bug (fixed mid-flight)

The `test_is_function_true` first draft included:
```
var f = fun (x) { return x + 1; };
print is_function(f);
```
This produces a parser error: `[line 6] Error: Expect expression.` because **clox doesn't support inline fun expressions** (the `fun` keyword is statement-only, not expression-only).

The fix was a test edit:
```
fun inc(x) { return x + 1; }
print is_function(inc);
```
**The discipline: clox has no inline fun expressions — declare the function first, then pass it.** This is the 17th test-bug in 49-stage history, and the same lesson as Stages 30, 32, 40, 44, 47, 48 (the inline-fun pattern).

**The new lesson for Stage 49:** "clox has no inline fun expressions" is a clox-specific design. Languages like JavaScript, Python, Ruby, and C# have lambda/anonymous function expressions (`fun (x) { return x + 1; }`, `lambda x: x + 1`, `->`, `x => x + 1`). clox's grammar restricts `fun` to statement position only, so the caller has to declare a named function first.

### 1 example bug (fixed BEFORE commit)

The `count_functions` section's first draft used `c -> c + 1` (inline fun expression). The fix was to declare `fun c(x) { return x + 1; }` first. **This is the same lesson as the test-bug — clox has no inline fun expressions.**

Also, the `call_or_default(clock, 0)` output comment was initially `<timestamp from clock()>` — but `clock()` returns a real timestamp that differs on each run. The fix was to use `e.g. 0.003724` (a specific example value, with the `e.g.` prefix to indicate it's not deterministic).

### Streak

**6th-consec-zero-impl-bug stage in the new streak** (Stage 44 was 1st; Stage 44's duplicate-pop bug reset the 17-streak from Stages 27-43 to 0; Stage 44 fixed and shipped at 1; Stage 45 was 2nd; Stage 46 was 3rd; Stage 47 was 4th; Stage 48 was 5th; Stage 49 is 6th). The new streak continues to grow.

The test-bug and example-bug don't break the impl-bug streak (the impl is correct, the tests were wrong).

## Architecture: zero new work

Stage 49 reuses Stages 44-48's type predicate pattern. The new wrinkle: **function-ness in clox has TWO sub-types — ObjClosure (Lox-defined) and ObjNative (C-defined).** `is_function` returns true for both, matching the JS/Python mental model.

The implementation pattern is similar to Stages 44-48 but with a multi-type check:
- argCount check
- `IS_OBJ(args[0])` check
- `OBJ_TYPE(args[0]) == OBJ_CLOSURE || OBJ_TYPE(args[0]) == OBJ_NATIVE` check
- Return `BOOL_VAL(true)` or `BOOL_VAL(false)`

The "type predicate" pattern is now COMPLETE with 6 natives (is_array, is_string, is_number, is_bool, is_nil, is_function). Each is ~10-32 lines, no new architecture, no user-code dispatch.

## Design decisions

1. **"Type predicates are a separate native pattern from type strings"** — `typeof()` returns a string (the type name); `is_function()` returns a bool (the predicate). The two are complementary: callers that need a string use `typeof()`; callers that need a yes/no answer use `is_*()`. The discipline: **type predicates are ergonomics, not replacements for type strings.**
2. **"Type predicates don't coerce"** — a string that contains the name of a function is NOT a function (just like is_array on a string is false even if the string says "function"). An array containing a function is NOT a function. A number is NOT a function. The alternative (loose coercion) loses type information. The discipline: **inspect the value's type tag, don't coerce the value.**
3. **"Function-ness has TWO sub-types"** — ObjClosure (Lox-defined) and ObjNative (C-defined). `is_function` returns true for both — matches the JS/Python mental model. The alternative is to have separate `is_closure()` and `is_native()` predicates, but that's a finer-grained split than callers usually want. The discipline: **the predicate's granularity should match the caller's question, not the VM's type system.**
4. **"clox has no inline fun expressions"** — clox's grammar restricts `fun` to statement position only. The caller has to declare a named function first, then pass it. The alternative is to add inline fun expressions (a grammar change), but that's a bigger swing. The discipline: **use the language as designed; don't add features the language doesn't have.**
5. **"is_function is the 6th and FINAL type predicate native"** — the pattern is now complete. Future stages won't add more type predicates (unless we discover a new type). The trigger-mine bucket either grows beyond byox or we ship modules.
6. **"Type predicates compose with control flow"** — the canonical use case is "type-dispatch on a value". With the 6 type predicates, the dispatch is clean: `is_function(x) ? ... : is_array(x) ? ... : ...`. Matches the discipline from Stages 30-48 of composing small primitives.
7. **"is_function returns BOOL_VAL, not OBJ_VAL"** — booleans in clox are represented as `BOOL_VAL(true)` or `BOOL_VAL(false)`, not as `OBJ_VAL(<bool object>)`. This matches clox's other type-checking natives.

## What this stage teaches

*(a) "The type predicate family is now COMPLETE (6 of 5+ natives)."* is_array (Stage 44), is_string (Stage 45), is_number (Stage 46), is_bool (Stage 47), is_nil (Stage 48), is_function (Stage 49). The pattern is established. Future stages won't add more type predicates (unless we discover a new type). The next step is to grow the trigger-mine bucket beyond byox or continue with array primitives.

*(b) "Function-ness has TWO sub-types."* ObjClosure (Lox-defined) and ObjNative (C-defined). is_function returns true for both — matches the JS/Python mental model. The alternative is to have separate is_closure() and is_native() predicates, but that's a finer-grained split than callers usually want.

*(c) "Type predicates don't coerce" — discipline from Stages 44-48 carries forward.* A string that contains the name of a function is NOT a function. An array containing a function is NOT a function. The alternative (loose coercion) loses type information.

*(d) **"Stage 44's push/pop lesson was applied for the 5th consecutive stage" — 0 impl bugs in Stage 49.** The new is_function registration has 1 push and 1 pop, balanced (vs. Stage 44's duplicate-pop bug). The discipline: **count pushes and pops after every registration.** The 5-second cost of counting is much less than the cost of a 221-test failure. Stage 49 is 6th-consec-zero-impl-bug in the new streak.

*(e) **"Test-bug caught: 17th test-bug in 49-stage history."** The first test of test_is_function_true used `var f = fun (x) { return x + 1; }` — clox doesn't support inline fun expressions (parser error: "Expect expression"). The fix was a test edit (use a named function instead). The discipline: **clox has no inline fun expressions — declare the function first, then pass it.**

*(f) **"Example-bug discipline applied" — 1 example bug caught and fixed BEFORE commit.** The count_functions section used `c -> c + 1` (inline fun expression). The fix was to declare `fun c(x)` first. The discipline: **verify the example output against the comments BEFORE commit** (13-occurrence pattern, the worst-class bug).

## Limitations

- **is_function doesn't distinguish ObjClosure from ObjNative** — both return true. The alternative (is_closure + is_native) would be a finer-grained split. Not part of this stage.
- **is_function doesn't inspect the function's arity or signature** — `is_function(noop)` and `is_function(inc)` both return true. The alternative (is_zero_arity, is_one_arity) would be future primitives. Not part of this stage.
- **is_function doesn't distinguish Lox closures from C-defined natives by their `typeof` output** — both return `"function"`. The alternative (is_user_defined, is_builtin) would be future primitives. Not part of this stage.
- **The test-bug and example-bug were caught pre-commit** — but the patterns (17 test-bugs, 13 example-bugs) show they're frequent hazards. The discipline: always verify before commit.

## My pick for Stage 50

After Stage 49, the "type predicate" pattern is COMPLETE. The remaining candidates are:
- `array_zip_longest(arr1, arr2, fill?) -> array` — like Stage 37's array_zip but pads the shorter with a default.
- `array_take(arr, n) -> array` / `array_drop(arr, n) -> array` — take/drop the first N elements.
- `array_take_while(arr, predicate) -> array` / `array_drop_while(arr, predicate) -> array` — take/drop while predicate is truthy.
- `array_intersect(arr1, arr2) -> array` / `array_union(arr1, arr2) -> array` / `array_difference(arr1, arr2) -> array` — set operations.
- **Modules** — ~600 lines, biggest swing. Tom's call, not mine.
- **Apply Stages 1-49 to a different project** — the trigger-mine bucket grows beyond byox.

**My pick for Stage 50: `array_zip_longest(arr1, arr2, fill?) -> array`** — like Stage 37's array_zip but pads the shorter with a default. ~30 lines, low risk, reuses Stage 37's architecture. The new wrinkle: the optional 3rd arg (the fill value) is a new pattern (Stage 50 introduces a "pad with default" semantic). The trigger-mine bucket is now 1 (byox) — picking the next non-type-predicate primitive.

**After Stage 50, the next decision is one of:**
1. **`array_take` / `array_drop`** — slice operations.
2. **`array_take_while` / `array_drop_while`** — short-circuit slice operations.
3. **`array_intersect` / `array_union` / `array_difference`** — set operations.
4. **Modules (~600 lines, Tom's call)** — the biggest swing.
5. **Apply Stages 1-49 to a different project** — the trigger-mine bucket grows beyond byox.

The next-pick for Stage 50 will be decided at the moment of decision, per the freeze-pattern antidote.

---

**File paths** (all confirmed):
- `05-vm/clox/src/native.c` — added `isFunctionNative` (32 lines) + 22 lines registration after `isNilNative`
- `05-vm/clox/tests/test_stdlib.c` — added 4 test functions + 4 main() invocations (121 lines)
- `05-vm/clox/examples/is-function.lox` — new example, 223 lines
- `05-vm/clox/docs/stage-49-closeout.md` — this file

**Branch:** `stage-49-is-function`, commit `d662566` (impl + tests + example) + this file's commit.
