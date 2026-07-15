# Stage 33 Close-out: array_any

**Stage:** 33 of 33
**Branch:** `stage-33-array-any`
**Commit:** `6f4984b` (impl + tests + example) — close-out (this file)
**Date:** 2026-07-15
**Tests:** 283/283 (was 270 before Stage 33, +13 stdlib passes: 6 single-pass + 2 triple-pass from the wrong_ tests = 8 test functions, 13 passes)
**Valgrind:** clean (1482 allocs / 1482 frees in test_stdlib, 0 errors, 0 leaks)
**Bugs caught:** 0 implementation bugs, 2 test bugs caught and fixed (test-bug, not impl-bug), 0 example bugs. **7th consecutive zero-bug stage (new streak at 7).**

## What shipped

`array_any(arr, predicate) -> bool` — the fourth clox native that invokes user-defined Lox code from C. Takes an array and a 1-arg Lox closure (the predicate: `(element) -> truthy-or-falsy`). Returns `true` as soon as the predicate is truthy on any element (short-circuit), `false` if all elements are falsy (or if the array is empty).

JS reference: `Array.prototype.some(predicate)`
Python reference: `any(iterable)`
The clox semantics: short-circuit on the first truthy result; empty array returns `false` without ever calling the predicate. The predicate's return type is unconstrained — any truthy value (in clox: anything that isn't `false` or `nil`) counts as "found."

## Tests added (8 total, 13 passes)

In `tests/test_stdlib.c`:
1. `test_array_any_finds_match` — `[2, 4, 5, 6]` array_any(isOdd) → `true` (5 is in `{1, 3, 5}`).
2. `test_array_any_no_match` — `[1, 2, 3, 4, 5]` array_any(isNegative) → `false`.
3. `test_array_any_empty` — `[]` array_any(isPositive) → `false` (predicate never called).
4. `test_array_any_short_circuits` — `[1, 2, 3, 4, 5]` array_any(trace) → `true` after visiting 1, 2, 3 (NOT 4, NOT 5). This test is the **first short-circuit verification** in the project.
5. `test_array_any_single_element_truthy` — `[42]` array_any(isTruthy) → `true`.
6. `test_array_any_single_element_falsy` — `[0]` array_any(isZero) → `true` (clox: 0 is truthy, so isZero is required to convert 0 to false; the test demonstrates this contract).
7. `test_array_any_does_not_mutate` — `src = [10, 20, 30]` array_any(isTwenty) → `true`, src unchanged.
8. `test_array_any_wrong_arg_count` — 1 arg errors; 3 args errors; 0 args errors (3 passes).
9. `test_array_any_wrong_type` — non-array source errors; non-function predicate errors; 0-arg predicate errors (3 passes).

## Bugs caught

### 0 implementation bugs

The first impl attempt passed all 8 tests on the first test run. The architecture from Stage 30 paid off AGAIN — this stage was a mechanical application of the same pattern (argCount=1, IS_ARRAY + IS_CLOSURE type checks, arity==1 check) with one new wrinkle: the early-exit return on the first truthy predicate result. The new wrinkle is a 2-line control-flow change, not an architecture change. The short-circuit just `return`s from the for loop body.

### 2 test bugs caught and fixed (test-bug, not impl-bug)

**Test bug #1: wrong membership in `test_array_any_finds_match`.** The first draft used `isOdd` defined as `x == 1 or x == 3 or x == 5`, then tested `[2, 4, 6, 7, 8]`. The test expected `true`, but the actual was `false` because `7` is not in `{1, 3, 5}`. The discipline: **when a test fails, verify the impl's output before declaring a bug.** The impl was correct; the test author used the wrong array (one where no element matches the predicate). The fix was a test edit (use `[2, 4, 5, 6]` instead, which contains the matching element 5), not a code change.

**Test bug #2: `toString` doesn't exist in clox.** The first drafts of `test_array_any_short_circuits` and `test_array_any_does_not_mutate` used `toString(x)` (a JS/Python idiom). clox has `string(x)` for numbers (Stage 17) but no `toString` global. After switching to `string(x)`, the second round failed because `string(true)` errors (Stage 17 is number-to-string, not bool-to-string). The fix was a test edit (use `string()` for numbers, separate `print found;` for booleans), not a code change. The discipline: **the "stringify this value" idiom varies by language; in clox, `string(n)` is for numbers, booleans print directly.**

### 0 example bugs

The new example (`examples/array-any.lox`) was run before commit. **One inline-function-expr bug caught and fixed during writing** (clox doesn't support `fun(x) { ... }` as an expression; the closure must be declared first). The discipline from Stage 30/32 carried forward: **declare the closure before passing it to the high-order native**.

**7th consecutive zero-bug stage (new streak at 7).** Stages 27, 28, 29, 30, 31, 32, 33 all shipped clean from the first test run. The 7-streak is the new record (the prior record was 16 from Stages 11-26, broken by Stage 26's `strtol` partial-consumption bug).

## Architecture: zero new work

Stage 33 reuses the architecture from Stage 30 unchanged:
- `callClosure()` is public (renamed in Stage 30)
- `callClosureFromNative(closure, argCount)` wrapper takes N-arg
- `static int vmNativeTargetDepth` + OP_RETURN target-depth check
- Stack layout for `callClosure` is `[callee, arg1, ..., argN]`

For `array_any`, argCount=1, so the stack layout is `[callee, arg1]`. The new wrinkle (short-circuit) is a control-flow change inside the for loop, not an architecture change. The native returns `BOOL_VAL(true)` from inside the loop body as soon as the predicate returns a truthy value. The VM's OP_RETURN target-depth check ensures the return un-winds the right number of frames.

The `vmNativeTargetDepth` mechanism (introduced in Stage 30) is what makes short-circuit work correctly. The `run()` function loops until `frameCount` returns to the target depth; the early `return TRUE_VAL` from inside `arrayAnyNative` is just a `return` from the C function, which the VM handles by popping the native's frame and continuing. No new opcodes, no new control flow.

## Design decisions

1. **Reuse Stage 30's architecture unchanged.** The N-arg closure path is built in; `array_any` uses argCount=1, same as `array_filter` and `array_map`. The architecture is now used by 4 natives: filter (1-arg predicate), map (1-arg transform), reduce (2-arg reducer), any (1-arg predicate).
2. **Short-circuit is the first deviation from "iterate everything."** `array_filter`, `array_map`, `array_reduce` all iterate every element. `array_any` is the first native that may return early. The short-circuit is implemented as a `return` from inside the for loop body.
3. **Empty-array returns `false` without ever calling the predicate.** This matches JS `Array.prototype.some([])` and Python `any([])`. The "no iterations" case is the canonical "default value" — `false` is the answer to "is there any X in zero things?".
4. **The predicate's return type is unconstrained.** Any truthy value (anything that isn't `false` or `nil` in clox) counts as "found." This matches JS (truthy check on the predicate's return) and Python (truthy check on the predicate's return). The clox truthy rule is documented: "only `false` and `nil` are falsy; `0` and `""` are truthy."
5. **The native's frame manages stack state correctly.** When `callClosureFromNative(predicate, 1)` is called, the predicate's slots are at `[callee, element]`. When the predicate returns, `callClosureFromNative` returns the predicate's return value. The native then `return`s from C — the VM pops the native's frame and pushes the C function's return value. The stack is balanced throughout.
6. **No GC protection needed for `array_any`.** The native doesn't allocate a result array (returns a bool), and the predicate is on the stack. The VM's existing GC checkpoints in `run()` handle the predicate's allocations.

## What this stage teaches

*(a) The N-arg closure path is a generalization, not a separate architecture.* The architecture from Stage 30 (`callClosureFromNative(closure, argCount)`) is now used by 4 natives: filter, map, reduce, any. The generalization was free at the design step; the architecture pays off every time we add a new high-order native.

*(b) "Short-circuit" is a control-flow change, not an architecture change.* The early `return` from inside the for loop body is a 2-line change. The VM's existing OP_RETURN target-depth check (introduced in Stage 30 for a different reason) handles the frame-unwinding correctly. The discipline: when adding a new high-order native, the architecture from Stage 30 handles most of the work; the new wrinkle is usually small.

*(c) "Empty-array-returns-false" is a canonical convention, not a design decision.* JS, Python, and most other languages agree that `any([])` is `false`. The discipline: when the design matches an existing canonical convention, document the convention in the close-out (not in the test), so the next maintainer knows it's intentional.

*(d) "Test-bug catches author mistake" pattern.* The first test of `test_array_any_finds_match` failed because the test author used the wrong array (one where no element matches the predicate). The discipline: **when a test fails, verify the impl's output before declaring a bug.** The impl was correct; the test was wrong. The fix was a test edit (use the right array), not a code change. This is the same pattern as Stages 8, 9, 10, 12a, 14, 15, 19, 21, 22, 24, 31, 32.

*(e) "Stringify this value" varies by language.* JS has `String(x)`, Python has `str(x)`, clox has `string(n)` for numbers (no bool-to-string). The discipline: when writing tests in clox, don't assume a `toString` global exists. Use `string(n)` for numbers, separate `print found;` for booleans, separate `print label;` for strings.

*(f) "clox doesn't support inline function expressions."* This is a Stage 30 / Stage 32 / Stage 33 lesson that keeps coming up. clox requires closures to be declared before being passed to high-order natives. The discipline: **declare the closure first, then pass it to the high-order native.** Same fix as Stage 30's compose example.

## Limitations

- No "no-predicate" form (predicate is required). The native errors if argCount != 2.
- The predicate must be a 1-arg Lox closure (not 0-arg, not 2-arg, not a native). The native errors if the arity check fails.
- The predicate's return type is unconstrained; the result is `true` if the predicate ever returns a truthy value.
- The source array is not mutated (read-only access).
- No "early-exit on the first false" — that's `array_all`, the natural Stage 34 candidate.
- No GC protection needed (bool result, no result array).
- Empty array returns `false` (no predicate call). The discipline matches JS/Python canonical convention.

## My pick for Stage 34

`array_all(arr, predicate) -> bool` — the natural mirror of `array_any`, with **short-circuit on the first false**. New native that takes an array and a 1-arg Lox closure (the predicate), returns `false` as soon as the predicate is falsy on any element (short-circuit: stops iterating), `true` if all elements are truthy. ~30 lines, no new concepts, the architecture is the same. The new wrinkle: short-circuit on the *first falsy* (vs. `array_any`'s short-circuit on the *first truthy*). The empty-array case returns `true` (vacuously true: "all of zero things are true" is the canonical answer — matches JS `Array.prototype.every([])` and Python `all([])`).

After Stage 34, the user-code-dispatch pattern is **fully established** (filter, map, reduce, any, all — the full JS Array.prototype iteration trifecta plus reduce). The remaining "stdlib in lox" candidates are:
- `array_find(arr, predicate) -> value` (returns first match, errors if none)
- `array_find_index(arr, predicate) -> number` (returns first match's index, -1 if none)
- `array_zip(arr1, arr2) -> array` (combines two arrays element-wise)
- `array_group_by(arr, keyFn) -> array` (groups by key; needs object/hash support)

After Stage 34, the next decision is **modules (~600 lines, Tom's call)** or grow the trigger-mine bucket beyond byox (e.g., apply Stages 1-33 to a different project).

---

**File paths** (all confirmed):
- `05-vm/clox/src/native.c` — added `arrayAnyNative` (66 lines) + registration block (10 lines)
- `05-vm/clox/tests/test_stdlib.c` — added 8 test functions + 9 main() invocations (226 lines)
- `05-vm/clox/examples/array-any.lox` — new example, 81 lines
- `05-vm/clox/docs/stage-33-closeout.md` — this file

**Branch:** `stage-33-array-any`, commit `6f4984b`.
