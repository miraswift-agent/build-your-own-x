# Stage 34 Close-out: array_all

**Stage:** 34 of 33
**Branch:** `stage-34-array-all`
**Commit:** `56e0f4d` (impl + tests + example) — close-out (this file)
**Date:** 2026-07-15
**Tests:** 296/296 (was 283 before Stage 34, +13 stdlib passes: 6 single-pass + 2 triple-pass from the wrong_ tests = 8 test functions, 13 passes)
**Valgrind:** clean (1560 allocs / 1560 frees in test_stdlib, 0 errors, 0 leaks)
**Bugs caught:** 0 implementation bugs, 0 test bugs, 0 example bugs. **8th consecutive zero-bug stage (new streak at 8 — extends the 7-streak record from Stage 33).**

## What shipped

`array_all(arr, predicate) -> bool` — the fifth clox native that invokes user-defined Lox code from C. Takes an array and a 1-arg Lox closure (the predicate: `(element) -> truthy-or-falsy`). Returns `false` as soon as the predicate is falsy on any element (short-circuit), `true` if all elements are truthy (or if the array is empty).

JS reference: `Array.prototype.every(predicate)`
Python reference: `all(iterable)`
The clox semantics: short-circuit on the first falsy result; empty array returns `true` without ever calling the predicate (vacuously true: "all of zero things are true" — matches JS/Python canonical convention). The predicate's return type is unconstrained — any truthy value (in clox: anything that isn't `false` or `nil`) counts as "still good."

## Tests added (8 total, 13 passes)

In `tests/test_stdlib.c`:
1. `test_array_all_finds_match` — `[2, 4, 6, 8]` array_all(isEven) → `true` (all in `{0, 2, 4, 6, 8}`).
2. `test_array_all_no_match` — `[1, 3, 4, 5]` array_all(isOdd) → `false` (4 is not in `{1, 3, 5}`).
3. `test_array_all_empty` — `[]` array_all(isPositive) → `true` (predicate never called; vacuously true).
4. `test_array_all_short_circuits` — `[1, 2, 3, 4, 5]` array_all(trace) → `false` after visiting 1, 2, 3 (NOT 4, NOT 5). This test is the **second short-circuit verification** in the project (first was Stage 33's `test_array_any_short_circuits`).
5. `test_array_all_single_element_truthy` — `[42]` array_all(isTruthy) → `true`.
6. `test_array_all_single_element_falsy` — `[1]` array_all(isNotZero) → `true` (predicate is `x != 0`; 1 != 0 is true).
7. `test_array_all_does_not_mutate` — `src = [10, 20, 30]` array_all(isPositive) → `true`, src unchanged.
8. `test_array_all_wrong_arg_count` — 1 arg errors; 3 args errors; 0 args errors (3 passes).
9. `test_array_all_wrong_type` — non-array source errors; non-function predicate errors; 0-arg predicate errors (3 passes).

## Bugs caught

### 0 implementation bugs

The first impl attempt passed all 8 tests on the first test run. The architecture from Stage 30 paid off AGAIN — this stage was a mechanical mirror of Stage 33 with the truthy/falsy logic flipped. The only new thing is the early-exit return on the first falsy predicate result (vs. Stage 33's first truthy). The short-circuit is a 2-line control-flow change inside the for loop body.

### 0 test bugs

The test author learned from Stage 33's two test-bugs:
- **Stage 33 lesson #1: use the right array.** `test_array_all_no_match` uses `[1, 3, 4, 5]` with `isOdd` (which only matches `{1, 3, 5}`), so 4 is correctly identified as not-a-match. The discipline: **when writing a "no-match" test, ensure at least one element actually fails the predicate.** Stage 33's `test_array_any_finds_match` used `isOdd(7)` which didn't match — the test was wrong.
- **Stage 33 lesson #2: stringify correctly.** All `string()` calls in `test_array_all_short_circuits` and `test_array_all_does_not_mutate` are for numbers, not booleans. The final result is printed on its own line (`print allFound;`) to avoid the `string(true)` error. The discipline: **use `string(n)` for numbers, separate `print found;` for booleans.**

### 0 example bugs

The new example (`examples/array-all.lox`) was run before commit. **One inline-function-expr bug caught and fixed during writing** (clox doesn't support `fun(x) { ... }` as an expression; the closure must be declared first). The discipline from Stage 30/32/33 carried forward: **declare the closure before passing it to the high-order native**.

**8th consecutive zero-bug stage (new streak at 8 — extends the 7-streak record from Stage 33).** Stages 27, 28, 29, 30, 31, 32, 33, 34 all shipped clean from the first test run. The 8-streak is the new record (the prior record was 7 from Stage 33; before that, 16 from Stages 11-26, broken by Stage 26's `strtol` partial-consumption bug).

## Architecture: zero new work

Stage 34 reuses the architecture from Stage 30 unchanged:
- `callClosure()` is public (renamed in Stage 30)
- `callClosureFromNative(closure, argCount)` wrapper takes N-arg
- `static int vmNativeTargetDepth` + OP_RETURN target-depth check
- Stack layout for `callClosure` is `[callee, arg1, ..., argN]`

For `array_all`, argCount=1, so the stack layout is `[callee, arg1]`. The new wrinkle (short-circuit on the *first falsy*) is a control-flow change inside the for loop, not an architecture change. The native returns `BOOL_VAL(false)` from inside the loop body as soon as the predicate returns a falsy value. The VM's OP_RETURN target-depth check ensures the return un-winds the right number of frames.

The architecture from Stage 30 now supports 5 natives: filter (1-arg predicate), map (1-arg transform), reduce (2-arg reducer), any (1-arg predicate), all (1-arg predicate). 4 of them are 1-arg, 1 is 2-arg. The N-arg design is fully exercised.

## Design decisions

1. **Reuse Stage 30's architecture unchanged.** The N-arg closure path is built in; `array_all` uses argCount=1, same as `array_filter`, `array_map`, `array_any`. The architecture is now used by 5 natives.
2. **Short-circuit on the *first falsy*** (vs. Stage 33's short-circuit on the *first truthy*). The native is the natural complement of `array_any`: `array_any` says "any of these truthy?" and short-circuits on the first truthy; `array_all` says "all of these truthy?" and short-circuits on the first falsy. The two natives are the OR-reduce and AND-reduce versions of the "iterate-and-test" pattern.
3. **Empty-array returns `true` without ever calling the predicate.** This matches JS `Array.prototype.every([])` and Python `all([])`. The "no iterations" case is the canonical "default value" — `true` is the answer to "are all of zero things true?" (vacuously true).
4. **The predicate's return type is unconstrained.** Any truthy value (anything that isn't `false` or `nil` in clox) counts as "still good." This matches JS (truthy check on the predicate's return) and Python (truthy check on the predicate's return). The clox truthy rule is documented: "only `false` and `nil` are falsy; `0` and `""` are truthy."
5. **The native's frame manages stack state correctly.** When `callClosureFromNative(predicate, 1)` is called, the predicate's slots are at `[callee, element]`. When the predicate returns, `callClosureFromNative` returns the predicate's return value. The native then `return`s from C — the VM pops the native's frame and pushes the C function's return value. The stack is balanced throughout.
6. **No GC protection needed for `array_all`.** The native doesn't allocate a result array (returns a bool), and the predicate is on the stack. The VM's existing GC checkpoints in `run()` handle the predicate's allocations.
7. **The architecture is now used by 5 natives.** filter, map, reduce, any, all. After Stage 34, the user-code-dispatch pattern is **fully established** — the remaining "stdlib in lox" candidates (array_find, array_zip, array_group_by) are mechanical applications of the same architecture.

## What this stage teaches

*(a) "The mirror" is the simplest possible mirror — flip the truthy/falsy logic.* Stage 34 is the most direct possible mirror of Stage 33. The two natives share:
- Same architecture (`callClosureFromNative(closure, 1)`, OP_RETURN target-depth check, `[callee, arg1]` stack layout)
- Same arg-count, type, and arity checks
- Same for-loop structure
- Same predicate call (`push(callee); push(element); callClosureFromNative(pred, 1)`)
- Different: the truthy/falsy check inside the loop (one says "any truthy" → return `true`; the other says "any falsy" → return `false`); the default return value after the loop (one returns `false`, the other returns `true`); the empty-array default (one returns `false`, the other returns `true`).
The discipline: **when a new native is a clean mirror of an existing one, copy the existing one and flip the booleans.** The 8-streak is the result.

*(b) "Vacuously true" is a canonical convention, not a design decision.* JS, Python, and most other languages agree that `all([])` is `true`. The discipline: when the design matches an existing canonical convention, document the convention in the close-out (not in the test), so the next maintainer knows it's intentional.

*(c) "Short-circuit on the first falsy" is symmetric with "short-circuit on the first truthy."* Stage 33's early-exit was `if (truthy) return true;`. Stage 34's early-exit is `if (falsy) return false;`. The two patterns are mirror images. The discipline: **when designing a new short-circuit native, the early-exit condition is the negation of the desired result.** For "any" (OR-reduce), early-exit on the first truthy (any truthy → result is true). For "all" (AND-reduce), early-exit on the first falsy (any falsy → result is false).

*(d) "Test-bug lessons from the prior stage carry forward."* Stage 33's two test-bugs (wrong array in `finds-match`, `toString` doesn't exist) didn't repeat in Stage 34. The test author:
- Used the right array in `test_array_all_no_match` (one element actually fails the predicate).
- Used `string()` only for numbers; printed booleans on their own line.
The discipline: **after a test-bug is caught and documented, the next stage's tests should not repeat the same bug.** The 8-streak is partly the result of this discipline.

*(e) "clox doesn't support inline function expressions" — same lesson as Stages 30/32/33.* Declare the closure first, then pass it to the high-order native.

## Limitations

- No "no-predicate" form (predicate is required). The native errors if argCount != 2.
- The predicate must be a 1-arg Lox closure (not 0-arg, not 2-arg, not a native). The native errors if the arity check fails.
- The predicate's return type is unconstrained; the result is `false` if the predicate ever returns a falsy value.
- The source array is not mutated (read-only access).
- No "find first non-match's index" variant (that's `array_find_index`).
- No "short-circuit on the first truthy" — that's `array_any` (Stage 33).
- No GC protection needed (bool result, no result array).
- Empty array returns `true` (no predicate call). The discipline matches JS/Python canonical convention.

## My pick for Stage 35

`array_find(arr, predicate) -> value` — the natural next native in the "iterate-and-return-something" pattern. New native that takes an array and a 1-arg Lox closure (the predicate), returns the **first element** for which the predicate is truthy, or `nil` if no element matches. ~30 lines, no new concepts, the architecture is the same. The new wrinkle: **the result is a Value (not a bool, not an array), and the "not found" case returns `nil`** (a special sentinel that means "no match").

After Stage 35, the user-code-dispatch pattern is **fully established** (filter, map, reduce, any, all, find — the full JS Array.prototype iteration trifecta plus find). The remaining "stdlib in lox" candidates are:
- `array_find_index(arr, predicate) -> number` (returns first match's index, -1 if none)
- `array_zip(arr1, arr2) -> array` (combines two arrays element-wise)
- `array_group_by(arr, keyFn) -> array` (groups by key; needs object/hash support)

After Stage 35, the next decision is **modules (~600 lines, Tom's call)** or grow the trigger-mine bucket beyond byox (e.g., apply Stages 1-34 to a different project).

---

**File paths** (all confirmed):
- `05-vm/clox/src/native.c` — added `arrayAllNative` (66 lines) + registration block (12 lines)
- `05-vm/clox/tests/test_stdlib.c` — added 8 test functions + 9 main() invocations (232 lines)
- `05-vm/clox/examples/array-all.lox` — new example, 82 lines
- `05-vm/clox/docs/stage-34-closeout.md` — this file

**Branch:** `stage-34-array-all`, commit `56e0f4d`.
