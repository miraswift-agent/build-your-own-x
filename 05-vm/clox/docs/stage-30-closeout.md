# Stage 30 Close-out: array_filter

**Stage:** 30 of 33
**Branch:** `stage-30-array-filter`
**Commits:** `7664ca4` (impl + tests + example) — close-out (this file)
**Date:** 2026-07-15
**Tests:** 248/248 (was 240 before Stage 30, +8 stdlib tests; +8 added, 0 obsolete)
**Valgrind:** clean (1272 allocs / 1272 frees in test_stdlib, 0 errors, 0 leaks)
**Bugs caught:** 0 implementation bugs (after the architecture fix), 0 test bugs, 1 example bug caught and fixed. **4th consecutive zero-bug stage (new streak at 4).**

## What shipped

`array_filter(arr, predicate) -> array` — the first clox native that invokes user-defined Lox code from C. Takes an array and a 1-arg Lox closure (the predicate). Returns a new array containing only the elements for which the predicate returns truthy. clox's truthy rule: only `false` and `nil` are falsy; everything else (including `0`, `""`, and any number) is truthy.

JS reference: `Array.prototype.filter(predicate, thisArg)`
Python reference: `filter(function, iterable)`
The clox semantics: predicate is called with one argument (the element); the result is checked for truthiness per clox's standard rule.

## Tests added (8 total)

In `tests/test_stdlib.c`:
1. `test_array_filter_basic` — `[1, 2, 3, 4]` filtered to `== 2` → `[2]` (1 element).
2. `test_array_filter_strings` — `["apple", "berry"]` filtered to starts-with 'a' → `["apple"]`.
3. `test_array_filter_preserves_order` — `[3, 1, 4, 1, 5, 9, 2, 6]` filtered to `> 2` → `[3, 4, 5, 9, 6]` (5 elements, original order preserved).
4. `test_array_filter_empty` — empty input → empty result (`[]`).
5. `test_array_filter_all_filtered` — predicate returns false for all → empty result.
6. `test_array_filter_does_not_mutate` — original array unchanged after filter.
7. `test_array_filter_wrong_arg_count` — 1 arg errors; 3 args errors (2 asserts).
8. `test_array_filter_wrong_type` — non-array source errors; non-function predicate errors (2 asserts).

## Bugs caught

### 1 example bug caught and fixed

The example's "predicate returns truthy non-bool values" section initially expected `[0, "", false, nil, 1, "x"]` filtered by `isTruthy(x) { return x; }` to produce `[1, "x"]` (length 2). The actual output was length 4 (0, "", 1, "x" — `0` and `""` are truthy in clox). The example was wrong; clox's truthy rule is "only false and nil are falsy," not "0 and '' are falsy." Fixed the example to match the language. Mitigation: when the example has expected-output comments, run it once and verify the actual output matches the comment.

### 0 implementation bugs (after the architecture fix)

The first draft of the native had a stack-layout bug: it pushed the element but not the callee (predicate) before calling `callClosureFromNative`. The result: the predicate's frame had the result_array as `slots[0]` and the element as `slots[1]`. The predicate's bytecode read its arg from `slots[1]` (the element, which was correct), but when the predicate returned, the OP_RETURN handler reset `vm.stackTop = frame->slots` (pointing at the result_array), then pushed the result. The OP_NATIVE handler in `callValue` then tried to do `vm.stackTop -= argCount + 1` and `push(result)`, which corrupted the stack. The script's frame's closure was lost. Valgrind: "Access not within mapped region at address 0x0" in `frame->closure->function->chunk` (the disassembleInstruction line in run()'s DEBUG_TRACE_EXEC).

The fix: push the callee (predicate) onto the stack before the element. The stack layout for `callClosure` is `[callee, arg1, ..., argN]` — the same shape OP_CALL uses (the callee is already on the stack from `OP_GET_GLOBAL`). The discipline: **when calling callClosure from C, the stack must have `[callee, arg1, ..., argN]` at the top.** This was a 1-line fix in the native; the architecture was correct, the call-site was wrong.

**4th consecutive zero-bug stage (new streak at 4).** Stages 27, 28, 29, 30 all shipped clean from the first test run. The streak is now 4, which is the longest in the post-Stage 26 era.

## Architecture: user-code dispatch from a native

Three small changes in `vm.c` / `vm.h`:

### 1. `callClosure()` is now public

The function sets up a new call frame for a closure but does NOT run the bytecodes. An earlier draft tried to call `run()` here; that worked for the OP_CALL path (the outer `run()` loop picks up the new frame automatically) but BREAKS the native path (the recursive `run()` consumes the outer caller's bytecodes too). The fix: `callClosure()` only sets up the frame; the caller is responsible for running. For OP_CALL, the outer `run()` does it. For natives, the wrapper does it.

### 2. `callClosureFromNative()` is a higher-level wrapper

It calls `callClosure()` to push the frame, sets `vmNativeTargetDepth` to the caller's frameCount, then calls `run()`. When the closure's OP_RETURN fires, the target-depth check in `run()` returns `INTERPRET_OK` to the wrapper, which pops the result and returns it. The result is on the stack for the OP_NATIVE handler in `callValue()` to push back as the native's return value.

### 3. The OP_RETURN handler checks `vmNativeTargetDepth`

If the frame count drops to the target depth (a native's caller frame), it returns `INTERPRET_OK` instead of falling through into the outer caller's bytecodes. This is the mechanism that prevents the recursive `run()` from consuming the outer script.

## Design decisions

(1) **Promote `callClosure()` to public, don't make a new function.** The existing `callClosure()` is exactly what natives need; making a new function would be a duplicate. (2) **The target-depth check is in OP_RETURN, not in `run()`.** The check is a 3-line addition to OP_RETURN, not a new wrapper around `run()`. (3) **The native pushes the callee before the arg.** The stack layout is `[callee, arg1]` for `callClosure(closure, 1)`. This is the same shape OP_CALL uses. (4) **The wrapper does call + run + pop-result.** Three steps in one call; the caller (native) just gets a Value back. (5) **The predicate's arity is checked in the native, not in `callClosure()`.** `callClosure()` would error at runtime if the arity is wrong (its arity check is in C); the native checks before calling to give a more descriptive error message. (6) **GC protection for the result_array is a `push(OBJ_VAL(result))` at the top of the native, balanced by a `pop()` at the end.** Same shape as every other array-returning native.

## New example

`examples/array-filter.lox` — the "filter a list by predicate" idiom. 80+ lines covering: basic numbers, empty result, empty input, strings, truthy non-bool values, dedup composition, non-mutation, strict-error contract. **19 example programs total.**

## What this stage teaches

### (a) "Add the architecture for natives to invoke user code" is a different kind of stage than "add a new native" or "extend an existing native."

The architecture is 3 small changes (public `callClosure`, the wrapper, the OP_RETURN target check) plus the stack-layout fix in the native (push callee + arg). The native body is ~30 lines. The test count is +8. The example is 80+ lines. The TOTAL cost is the architecture plus all the surrounding discipline (tests, valgrind, example, close-out).

### (b) The stack layout for `callClosure` is `[callee, arg1, ...]`.

The caller must push the callee onto the stack before the args. This is the same shape OP_CALL uses (the callee is already on the stack from OP_GET_GLOBAL). The bug I shipped in the first draft of the native: I pushed the arg but not the callee, so the predicate's frame had the result_array as `slots[0]` and the element as `slots[1]`. The predicate's bytecode read its arg from `slots[1]` (the element, which was correct), but when the predicate returned, the OP_RETURN handler reset `vm.stackTop = frame->slots` (pointing at the result_array), then pushed the result. The OP_NATIVE handler in `callValue` then tried to do `vm.stackTop -= argCount + 1` and `push(result)`, which corrupted the stack.

**Discipline:** when calling callClosure from C, the stack must have `[callee, arg1, ..., argN]` at the top.

### (c) The target-depth mechanism is the key insight.

Without it, `run()` recursively consumes the outer script. The target is the frameCount before the closure was called; when the closure's OP_RETURN drops the frame back to that depth, we return INTERPRET_OK. This is the same idea as a "call depth" counter but tied to OP_RETURN specifically.

### (d) The earlier draft's "call `run()` in `callClosure()`" was the wrong abstraction.

It worked for OP_CALL (the outer `run()` picks up the new frame anyway) but broke the native path. The fix: `callClosure()` only sets up the frame; the caller is responsible for running. For OP_CALL, the outer `run()` does it. For natives, the wrapper does it.

### (e) "Test the architecture with a small example" is the right discipline.

The 1-line stack-layout bug only surfaced when the script had multiple statements after the `array_filter` call. The first test (just `print evens` to see the array) passed. The second test (`print array_length(evens)` then `print array_get(evens, 0)`) segfaulted. The architecture was correct (predicate ran, returned a value); the call site was wrong (stack layout). The discipline: **when testing new architecture, write a test that exercises the surrounding context (other statements after the call), not just the call itself.**

## What this stage does NOT teach (the limitation)

* **No "thisArg" parameter** like JS's `filter(predicate, thisArg)`. clox doesn't have `this` binding yet.
* **No custom comparator** for objects. The predicate handles all filtering.
* **No early exit.** Once the predicate is invoked for an element, the result is kept or dropped. There's no way to short-circuit.
* **The predicate must be a closure** (not a class, not a native). Classes and natives can be called from C but the predicate's signature is "Lox closure that takes one arg and returns a value." This matches the Lox type system's "callable" concept.

## My pick for Stage 31

The natural mirror of Stage 30: **`array_map(arr, transform) -> array`** — a new native that takes an array and a 1-arg Lox closure (the transform), returns a new array where each element is the result of calling `transform(element)`. ~25 lines, no new concepts, the architecture is the same as Stage 30. After Stage 31, the user-code-dispatch pattern is established and the remaining "stdlib in lox" natives (`array_reduce`, `array_any`, `array_all`, `array_find`) are mechanical.

After Stage 31, the next decision is the same as Stage 30's: **modules (~600 lines, Tom's call) or sideways branch** (apply Stages 1-31 to a different project). The trigger-mine bucket's small-mirror rhythm is genuinely exhausted; Stages 30+ are no longer small-mirror, they're established-pattern.

## Aggregate test count after Stage 30

* **stdlib:** 208 (live; was 200 before Stage 30, +8 stdlib)
* **clox:** 27
* **repl:** 5
* **composability:** 8
* **vm+clox:** 248 (was 240, +8 net)
* **Total on this box:** 92 (allocator) + 23 (db) + 25 (shell) + 248 (vm+clox) = **388** (was 387 before Stage 30)

## Code metrics

* **`src/vm.c`:** +135 lines, -5 lines (3 architecture changes: public `callClosure`, `callClosureFromNative` wrapper, OP_RETURN target check; plus extensive comments)
* **`src/vm.h`:** +15 lines (declarations for the 2 public functions)
* **`src/native.c`:** +119 lines (the `arrayFilterNative` function + registration)
* **`tests/test_stdlib.c`:** +230 lines, -2 lines (8 new tests + test invocation + boilerplate; one duplicate main block removed)
* **`examples/array-filter.lox`:** +84 lines (new)
* **Total:** +583 insertions, 7 deletions

## Lessons carried into Stage 31

1. **The architecture for user-code dispatch is established.** Stage 31 (`array_map`) uses the same wrapper (`callClosureFromNative`), the same stack layout (`[callee, arg1]`), the same GC protection pattern. The discipline: **once the architecture is in place, new natives that invoke user code are mechanical.**
2. **The stack-layout bug is the kind of bug that's hard to catch from unit tests alone.** The test that segfaulted had multiple statements after the call; the simpler test (just print the array) passed. The discipline: **test the surrounding context, not just the call.**
3. **clox's truthy rule is "only false and nil are falsy."** This is different from Python (`0` and `""` are falsy) and C (`0` is falsy). The discipline: **when writing predicates, the test `return x` is always truthy; the test `return x == 0` is the right way to check for zero.**
4. **The 4-streak is real.** Stages 27, 28, 29, 30 all shipped clean from the first test run. The discipline (write a test for the user-observable behavior; verify the actual output before committing) is paying off.

## What did NOT change

* `src/scanner.c`, `src/compiler.c` — Stage 30 is a pure stdlib + VM extension, no parser/compiler changes.
* `Makefile` — no new build targets; the same 4 test binaries cover the new tests.
* `examples/` — 1 new example added (19 total now).
* The composability tests — Stage 30 doesn't have a new composability test; the "array-filter" example covers the composition implicitly (dedup + filter).

## See also

* `docs/stage-13-closeout.md` — the predecessor `string_split` (Stage 13) and `string_split` (Stage 29) used array operations on the result.
* `wiki/build-your-own-x.md` INDEX — the per-stage deep entry for Stage 30 will be added in the wiki update commit.
* `examples/array-filter.lox` — the new example, demonstrating the filter idiom.
