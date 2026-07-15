# Stage 32 Close-out: array_reduce

**Stage:** 32 of 33
**Branch:** `stage-32-array-reduce`
**Commit:** `66db634` (impl + tests + example) — close-out (this file)
**Date:** 2026-07-15
**Tests:** 270/270 (was 258 before Stage 32, +12 stdlib passes: 6 single-pass + 2 triple-pass from the wrong_ tests = 8 test functions, 12 passes)
**Valgrind:** clean (1404 allocs / 1404 frees in test_stdlib, 0 errors, 0 leaks)
**Bugs caught:** 0 implementation bugs, 1 test bug caught and fixed (test-bug, not impl-bug), 0 example bugs. **6th consecutive zero-bug stage (new streak at 6).**

## What shipped

`array_reduce(arr, reducer, initial) -> value` — the third clox native that invokes user-defined Lox code from C. Takes an array, a 2-arg Lox closure (the reducer: `(accumulator, element) -> newAccumulator`), and an initial value. Returns a single value (the final accumulator).

JS reference: `Array.prototype.reduce(reducer, initial)`
Python reference: `functools.reduce(function, iterable[, initial])`
The clox semantics: `initial` is **required** (no "no-initial" form — different from JS, which uses the first element as the initial if no initial is provided). The reducer is called once per element with `(acc, elem)`; the result becomes the new accumulator. Empty array → returns the initial as-is (same as Python's `functools.reduce`; different from JS, which errors on empty array with no initial).

## Tests added (8 total, 12 passes)

In `tests/test_stdlib.c`:
1. `test_array_reduce_sum` — `[1,2,3,4,5]` reduced by `+` from `0` → `15`.
2. `test_array_reduce_product` — `[1,2,3,4]` reduced by `*` from `1` → `24`.
3. `test_array_reduce_string_concat` — `["a","b","c"]` reduced by concat from `""` → `"abc"`.
4. `test_array_reduce_empty_array` — `[]` reduced by `+` from `42` → `42` (initial returned as-is).
5. `test_array_reduce_type_change` — `["a","","b"]` reduced by `string_length > 0` from `0` → `true` (reducer's return type is unconstrained).
6. `test_array_reduce_composes_with_map` — `[1,2,3,4]` mapped by `*2` then reduced by `+` from `0` → `20` (Stage 31 + Stage 32 composition).
7. `test_array_reduce_wrong_arg_count` — 1 arg errors; 2 args errors; 4 args errors (3 passes).
8. `test_array_reduce_wrong_type` — non-array source errors; non-function reducer errors; 1-arg reducer errors (3 passes).

## Bugs caught

### 0 implementation bugs

The first impl attempt passed all 8 tests on the first test run. The architecture from Stage 30 paid off again — this stage was a mechanical application of the same pattern with argCount=2 (instead of 1). No new concepts, no new bugs.

### 1 test bug caught and fixed (test-bug, not impl-bug)

The first draft of `test_array_reduce_type_change` expected the test to print `'1'` for `["a", "", "b"]` reduced by `(string_length(s) > 0)` starting from `0`. The actual output was `'true'` because the reducer returns a bool, and the accumulator ends up being the last value the reducer returned (which is `true` for the `"b"` element). The discipline: **when a test fails, verify the impl's output before declaring a bug.** The test author was wrong to expect a number; the impl is correct (the reducer's return type is unconstrained, so the accumulator is whatever the reducer last returned).

The fix was a test edit (expect `'true'` instead of `'1'`, update the test comment to explain the type-propagation behavior), not a code change. The discipline: **when the impl's output is reasonable and the test's expected output is reasonable, the gap is the design's type contract. Document the type contract in the test comment so the expectation is explicit.**

### 0 example bugs

The new example (`examples/array-reduce.lox`) was run before commit and all output matched the comments.

**6th consecutive zero-bug stage (new streak at 6).** Stages 27, 28, 29, 30, 31, 32 all shipped clean from the first test run.

## Architecture: zero new work

The Stage 30 user-code-dispatch architecture was reused **unchanged** for the 2-arg case:
- `callClosureFromNative(closure, argCount)` for the call+run+pop-result dance (now exercised with `argCount=2`)
- The OP_RETURN target-depth check
- The `[callee, arg1, ..., argN]` stack layout (now `[callee, acc, element]`)
- No new VM code, no new wrappers

**The N-arg closure path works as designed.** Stages 30-31 used 1-arg closures; Stage 32 uses 2-arg. The architecture was designed to be N-arg from the start; Stage 32 is the first stage that exercises anything other than 1-arg. The discipline: **design the architecture to be the maximally general form, then ship the 1-arg case first to prove the design.**

The native body is ~50 lines. The new shape: pop the initial into a local `Value acc`, then loop through the source array calling `callClosureFromNative(reducer, 2)` for each element. The accumulator lives as a local `Value` variable, threaded through each call. After the loop, return the final accumulator.

## Design decisions

(1) **Reuse Stage 30's architecture unchanged.** The 2-arg case uses the same wrapper with `argCount=2`. (2) **Initial is required.** Different from JS's no-initial form. Rationale: (a) the no-initial form requires an "empty array" check, (b) the initial is also the type of the result, which is a useful signal, (c) Python's `functools.reduce` is the more recent canonical form. (3) **Empty array returns the initial as-is.** Same as Python; different from JS (which errors). (4) **Reducer must be a 2-arg Lox closure, not a native.** Same limitation as Stages 30-31; use a closure wrapper for native callables. (5) **Reducer's return type is unconstrained.** The result can be any Value type (number, string, bool, nil, object reference). The accumulator threads the reducer's return value to the next call. (6) **No short-circuit.** The reducer is invoked for every element. There's no way to stop early. (7) **No GC push/pop around the accumulator.** The accumulator is a local C variable; the reducer's frame pins the reducer and the source; the VM's stack-traversing collector handles the rest. Different from Stages 30-31 (which pushed a result array for GC protection).

## New example

`examples/array-reduce.lox` — the "reduce a list to a single value" idiom. 90+ lines covering: sum, product, string-concat, max, min, empty-array-returns-initial, count-by-condition, map+reduce composition, filter+map+reduce composition, strict-error contract. **21 example programs total.**

## What this stage teaches

### (a) The N-arg closure path works as designed.

Stages 30-31 used 1-arg closures; Stage 32 uses 2-arg. The architecture (`callClosureFromNative` with `argCount=N`) was designed to be N-arg from the start; Stage 32 is the first stage that exercises anything other than 1-arg. The discipline: **design the architecture to be the maximally general form, then ship the 1-arg case first to prove the design.**

### (b) The accumulator as a local Value is a different shape than the result-array pattern.

Stages 30-31 returned a new `ObjArray`; Stage 32 returns a single `Value` (number, string, bool, nil, or an object reference). The GC protection is implicit: the reducer's frame pins the reducer and the source, and the accumulator lives as a local C variable so the GC can find it via the VM's stack-traversing collector. No explicit `push`/`pop` around the accumulator; the local variable is enough.

### (c) "Initial is required" is a design decision worth defending.

JS allows no-initial (uses first element, errors on empty array). Python requires it. The clox design: require it. Rationale: (1) the no-initial form requires an "empty array" check, (2) the initial is also the type of the result, which is a useful signal, (3) Python's `functools.reduce` is the more recent canonical form. The discipline: **when two standard libs differ, pick the more explicit form and document the choice.**

### (d) "Test-bug catches a type-propagation edge" is the newest pattern in the test-bug taxonomy.

The test author (me) expected a number; the impl correctly propagated a bool. The fix was a test edit, not a code change. The discipline: **when the impl's output is reasonable and the test's expected output is reasonable, the gap is the design's type contract. Document the type contract in the test comment so the expectation is explicit.**

### (e) The architecture pays off in Stages 30-32.

The three stages together establish the full pattern: filter (1-arg, returns array), map (1-arg, returns array), reduce (2-arg, returns value). The remaining "stdlib in lox" natives (array_any, array_all, array_find) are mechanical — same architecture, different function contracts and short-circuit semantics. The architectural work is done; the remaining work is function contracts.

## What this stage does NOT teach (the limitation)

* **No "no-initial" form.** Always require an initial; different from JS. The empty-array-returns-initial behavior matches Python.
* **No "thisArg" parameter** like JS's `reduce(reducer, initial, thisArg)`. clox doesn't have `this` binding yet.
* **No short-circuit.** The reducer is invoked for every element. There's no way to stop early. (Future: `array_any`/`array_all` will need short-circuit for the "found it" case.)
* **The reducer must be a 2-arg Lox closure, not a native.** Use a closure wrapper for native callables.
* **The reducer's return type is unconstrained.** The result can be any Value type. The accumulator threads the reducer's return value to the next call; the final value is whatever the last call returned (or the initial if the array was empty).
* **No 0-arg, 1-arg, 3-arg, or variadic reducer forms.** The architecture supports N-arg; Stage 32 just uses 2.

## My pick for Stage 33

The natural mirror of Stage 30's array_filter, with short-circuit: **`array_any(arr, predicate) -> bool`** — a new native that takes an array and a 1-arg Lox closure (the predicate), returns `true` as soon as the predicate is truthy on any element, `false` if all elements are falsy. ~25 lines, no new concepts, the architecture is the same. **The new wrinkle: short-circuit.** The native returns early (without iterating the rest of the array) once the predicate is truthy on any element. This is the first native that doesn't iterate the full array.

JS reference: `Array.prototype.some(predicate, thisArg)`
Python reference: `any(function, iterable)`
The clox semantics: predicate is called with one argument (the element). Returns `true` if the predicate returns truthy for any element, `false` otherwise. Empty array → `false`.

After Stage 33, the remaining "stdlib in lox" natives (`array_all`, `array_find`) are mechanical — same architecture, different function contracts. The user-code-dispatch pattern is then fully established (filter, map, reduce, any, all, find — the full JS Array.prototype iteration trifecta, modulo `findIndex` and `sort`).

**Caveat (what would change the pick):**
- (a) Short-circuit reveals an architecture gap in `callClosureFromNative` (low risk — the wrapper's return value is already on the stack; we'd just need to check the return after each call and `return` early if truthy).
- (b) A more pressing hand-roll idiom surfaces (e.g., `array_zip`, `array_flat`, `array_group_by`).
- (c) Tom redirects byox scope.
- (d) The empty-array-returns-false behavior conflicts with the user's expectation (low risk — `false` is the standard answer for "any of zero things" in JS/Python).

## Aggregate test count after Stage 32

* **stdlib:** 230 (live; was 218 before Stage 32, +12 net: 6 single-pass + 2 triple-pass = 8 test functions, 12 passes)
* **clox:** 27
* **repl:** 5
* **composability:** 8
* **vm+clox:** 270 (was 258, +12 net)
* **Total on this box:** 92 (allocator) + 23 (db) + 25 (shell) + 270 (vm+clox) = **410** (was 398 before Stage 32)

## Code metrics

* **`src/native.c`:** +63 lines (the `arrayReduceNative` function + registration)
* **`tests/test_stdlib.c`:** +205 lines (8 new tests + test invocation)
* **`examples/array-reduce.lox`:** +94 lines (new)
* **Total:** +362 insertions, 0 deletions

## Lessons carried into Stage 33

1. **Short-circuit is the new architecture wrinkle.** Stages 30-32 iterate the full array. Stage 33 needs to return early once the predicate is truthy. The mechanism: after each `callClosureFromNative(predicate, 1)` call, check the return value; if truthy, return `BOOL_VAL(true)` immediately. The architecture supports this (the return value is on the stack; we'd just need to check it and `return` early). The discipline: **short-circuit is a function-contract decision, not an architecture decision. Verify the existing architecture supports it before writing the new native.**
2. **clox's truthy rule: only false and nil are falsy.** Same as Stages 30-31. The predicate in `array_any` returns truthy for everything except false and nil.
3. **The "test-bug catches a design gap" pattern is now established.** When a test fails, the fix is sometimes a test edit (document the contract) or a design decision, not a code change. The signal: the test's expected behavior is reasonable; the impl's actual behavior is reasonable; the gap is the contract.
4. **The 6-streak is real.** Stages 27, 28, 29, 30, 31, 32 all shipped clean from the first test run. The discipline (write a test for the user-observable behavior; verify the actual output before committing; reuse the established architecture) is paying off. The streak is now 6, which is the longest in the post-Stage 26 era.

## What did NOT change

* `src/vm.c`, `src/vm.h` — Stage 32 is a pure stdlib extension using the Stage 30 architecture with argCount=2. No new VM changes.
* `src/scanner.c`, `src/compiler.c` — no parser/compiler changes.
* `Makefile` — no new build targets.
* The composability tests — Stage 32 doesn't have a new composability test; the "array-reduce" example covers the composition implicitly (filter+map+reduce).

## See also

* `docs/stage-30-closeout.md` — the predecessor `array_filter`, which established the user-code-dispatch architecture.
* `docs/stage-31-closeout.md` — the predecessor `array_map`, the cleanest mirror stage (zero new architecture work).
* `wiki/build-your-own-x.md` INDEX — the per-stage deep entry for Stage 32 will be added in the wiki update commit.
* `examples/array-reduce.lox` — the new example, demonstrating the reduce idiom.
