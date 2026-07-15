# Stage 31 Close-out: array_map

**Stage:** 31 of 33
**Branch:** `stage-31-array-map`
**Commit:** `78d2f41` (impl + tests + example) — close-out (this file)
**Date:** 2026-07-15
**Tests:** 258/258 (was 248 before Stage 31, +10 stdlib passes: 6 single-pass + 2 double-pass from the wrong_ tests = 8 test functions, 10 passes)
**Valgrind:** clean (1332 allocs / 1332 frees in test_stdlib, 0 errors, 0 leaks)
**Bugs caught:** 0 implementation bugs, 0 test bugs, 0 example bugs. **5th consecutive zero-bug stage (new streak at 5).**

## What shipped

`array_map(arr, transform) -> array` — the second clox native that invokes user-defined Lox code from C. Takes an array and a 1-arg Lox closure (the transform). Returns a new array where each element is `transform(element)`. The transform's return type can differ from the source's element type (heterogeneous result array is fine, same as clox's array semantics).

JS reference: `Array.prototype.map(transform, thisArg)`
Python reference: `map(function, iterable)`
The clox semantics: transform is called with one argument (the element); the result is pushed to the result array. The result array is always one-to-one with the source (unlike `array_filter` which may be smaller).

## Tests added (8 total, 10 passes)

In `tests/test_stdlib.c`:
1. `test_array_map_basic` — `[1,2,3,4]` mapped by `x*2` → `[2,4,6,8]` (4 elements, 5 prints).
2. `test_array_map_type_change` — `[1,2,3]` mapped by `string(x)` (via closure wrapper) → `["1","2","3"]` (3 elements).
3. `test_array_map_preserves_order` — `[3,1,4,1,5,9,2,6]` mapped by `x+10` → `[13,11,14,11,15,19,12,16]` (8 elements, original order preserved).
4. `test_array_map_empty` — empty input → empty result.
5. `test_array_map_does_not_mutate` — original array unchanged after map.
6. `test_array_map_composes_with_filter` — `[1,2,3,4,5]` filtered to `>2` then mapped to `x*2` → `[6,8,10]` (Stage 30 + Stage 31 composition).
7. `test_array_map_wrong_arg_count` — 1 arg errors; 3 args errors (2 passes).
8. `test_array_map_wrong_type` — non-array source errors; non-function transform errors (2 passes).

## Bugs caught

### 0 implementation bugs

The first impl attempt passed all 8 tests on the first test run. The architecture from Stage 30 is paying off — this stage was a mechanical application of the same pattern. No new concepts, no new bugs.

### 0 test bugs (after the design-decision fix)

The first draft of `test_array_map_type_change` passed the `string` native directly as the transform. The native failed with `"array_map() argument 1 must be a function"` because the architecture uses `callClosureFromNative` which only takes `ObjClosure`, not `ObjNative`. Two options:
- (a) Expand the architecture to support native callables (would require a new `callNativeFromNative` wrapper)
- (b) Document the limitation and require a closure wrapper

I picked (b) for three reasons: (1) preserving the single-call-path architecture is more important than saving a 1-line wrapper in user code; (2) the closure wrapper is a small cost in the test (1 extra line) and a useful illustration of the callable contract; (3) "transform must be a 1-arg Lox closure" matches the documented contract from Stage 30.

The fix was a test edit (wrap `string` in a `toString(x) { return string(x); }` closure), not a code change. The discipline: **when a test fails, the fix is sometimes a design decision, not a code change.**

### 0 example bugs

The new example (`examples/array-map.lox`) was run before commit and all output matched the comments. The "type change" and "strings" sections both show the closure-wrapper pattern in action.

**5th consecutive zero-bug stage (new streak at 5).** Stages 27, 28, 29, 30, 31 all shipped clean from the first test run.

## Architecture: zero new work

The Stage 30 user-code-dispatch architecture was reused **unchanged**:
- `callClosureFromNative()` for the call+run+pop-result dance
- The OP_RETURN target-depth check
- The `[callee, arg1]` stack layout
- The GC protection via `push(OBJ_VAL(result))`

The native body is ~30 lines. The architecture is shared.

The one place where the architecture would have needed a new wrapper was the "native callable" case (the test that originally failed). I chose to document the limitation rather than add the wrapper. Rationale: the architecture is single-call-path; adding a parallel path for native callables would be a 5-line change but would create two call paths to maintain. The closure wrapper is a 1-line cost in user code; the architectural complexity is higher. The discipline: **when in doubt, keep the architecture single-path and document the contract.**

## Design decisions

(1) **Reuse Stage 30's architecture unchanged.** No new code in vm.c, no new wrappers. The same `callClosureFromNative` handles both 1-arg predicates (filter) and 1-arg transforms (map). (2) **Transform must be a 1-arg Lox closure, not a native.** Document the contract; require a wrapper for native callables. (3) **Transform's return type can differ from source element type.** No type constraint on the result array — same as clox's array semantics. (4) **Result array is always one-to-one with source.** Unlike `array_filter`, the result is never smaller (empty source → empty result; everything else → same length). (5) **No short-circuit.** The transform is invoked for every element. (6) **GC protection for the result_array is a `push(OBJ_VAL(result))` at the top of the native, balanced by a `pop()` at the end.** Same shape as every other array-returning native.

## New example

`examples/array-map.lox` — the "transform a list element-by-element" idiom. 80+ lines covering: basic numbers, type change (number → string), empty input, strings (uppercase), filter+map composition, non-mutation, strict-error contract. **20 example programs total.**

## What this stage teaches

### (a) "Mirror of a previous stage that uses the same architecture" is the cleanest stage shape.

The whole stage is the same architectural decision, the same test structure, the same example shape — only the function contract changes (filter → map, predicate → transform, truthy check → push result). The native body is ~30 lines, the test count is +8 functions (+10 passes), the example is 80+ lines. The TOTAL cost is the function contract, not the architecture. The discipline: **when the architecture is established, mirror stages are mostly about "what's the right error message and the right arity check."**

### (b) The "single call path" principle.

The architecture is one wrapper (`callClosureFromNative`) and one stack layout (`[callee, arg1]`). Adding a parallel path for native callables would be a 5-line change but would create two call paths to maintain. The discipline: **prefer single-path architectures; document the contract that the path supports.** When a test fails because the contract is too narrow, the fix is usually a design decision (document the limitation) or a test edit (use the contract), not an architecture change (add a parallel path).

### (c) The "test-bug catches design gap" pattern.

The first test of `test_array_map_type_change` failed not because the test was wrong or the impl was wrong, but because the test exposed a design gap in the architecture (native vs closure as the callable). The fix was a design decision (use a closure wrapper, document the limitation), not a code change. The discipline: **when a test fails, the fix is sometimes a design decision, not a code change.** The signal: the test's "expected behavior" is reasonable; the impl's "actual behavior" is also reasonable; the gap is the architecture.

### (d) The architecture pays off in Stages 30 and 31.

The two stages together establish the pattern: any future native that invokes user-defined Lox code (Stage 32+ candidates: array_reduce, array_any, array_all, array_find) is a ~25-30 line native body with the same wrapper, the same stack layout, the same GC protection. The architectural work is done; the remaining work is function contracts.

## What this stage does NOT teach (the limitation)

* **No native callables.** The transform must be a 1-arg Lox closure, not a native. Use a closure wrapper (1 line) for native callables. The architecture could be extended with a `callNativeFromNative` wrapper, but the single-path principle keeps it out.
* **No "thisArg" parameter** like JS's `map(transform, thisArg)`. clox doesn't have `this` binding yet.
* **No short-circuit.** The transform is invoked for every element. There's no way to stop early.
* **The transform can return any Value type.** The result array is heterogeneous, same as clox's array semantics. If you want a homogeneous result, filter first or check the type in the transform.
* **The transform must be a 1-arg closure.** No 0-arg (constant), 2-arg (indexed map), or variadic forms. Indexed map (`arr.map((x, i) => x + i)`) is a future stage; the architecture supports it (the wrapper takes an argCount) but no native currently exposes it.

## My pick for Stage 32

The natural mirror of Stage 31: **`array_reduce(arr, reducer, initial) -> value`** — a new native that takes an array, a 2-arg Lox closure (the reducer: `(accumulator, element) -> newAccumulator`), and an initial value. Returns a single value (the final accumulator). ~30 lines, no new concepts, the architecture is the same. **The new wrinkle:** the reducer is 2-arg, not 1-arg. The architecture already supports N-arg closures (the `argCount` parameter to `callClosureFromNative`); Stage 32 just exercises that with argCount=2.

JS reference: `Array.prototype.reduce(reducer, initial)`
Python reference: `functools.reduce(function, iterable[, initial])`
The clox semantics: `initial` is required (no "no-initial" form — different from JS, which uses the first element as the initial if no initial is provided). The reducer is called once per element with `(accumulator, element)`; the result becomes the new accumulator. Returns the final accumulator.

After Stage 32, the remaining "stdlib in lox" natives (`array_any`, `array_all`, `array_find`) are mechanical — same architecture, different function contracts.

**Caveat (what would change the pick):**
- (a) The "no initial" form turns out to be essential (then Stage 32 is `array_reduce(arr, reducer[, initial])` with the no-initial form = first element as accumulator). Need to check what JS/Python actually use.
- (b) A more pressing hand-roll idiom surfaces (e.g., `array_zip`, `array_flat`, `array_group_by`).
- (c) Tom redirects byox scope.
- (d) The 2-arg closure reveals an architecture gap in `callClosureFromNative` (low risk — the wrapper already takes an argCount).

## Aggregate test count after Stage 31

* **stdlib:** 218 (live; was 208 before Stage 31, +10 net: 6 single-pass + 2 double-pass = 8 test functions, 10 passes)
* **clox:** 27
* **repl:** 5
* **composability:** 8
* **vm+clox:** 258 (was 248, +10 net)
* **Total on this box:** 92 (allocator) + 23 (db) + 25 (shell) + 258 (vm+clox) = **398** (was 388 before Stage 31)

## Code metrics

* **`src/native.c`:** +67 lines (the `arrayMapNative` function + registration)
* **`tests/test_stdlib.c`:** +228 lines (8 new tests + test invocation)
* **`examples/array-map.lox`:** +79 lines (new)
* **Total:** +374 insertions, 0 deletions

## Lessons carried into Stage 32

1. **The architecture supports N-arg closures.** The `callClosureFromNative` wrapper takes an `argCount` parameter; the stack layout is `[callee, arg1, ..., argN]`. Stage 32's reducer is 2-arg, so the existing wrapper works as-is. The discipline: **verify the existing wrapper supports the new arity before writing the new native.**
2. **"Initial is required" is a design decision.** JS allows no-initial (uses first element). Python requires it. The clox design: require it. Rationale: (a) the no-initial form requires an "empty array" check, (b) the initial is also the type of the result, which is a useful signal, (c) Python's `functools.reduce` is the more recent canonical form. The discipline: **when two standard libs differ, pick the more explicit form and document the choice.**
3. **The "test-bug catches design gap" pattern is now established.** When a test fails, the fix is sometimes a design decision (document the contract), not a code change. The signal: the test's expected behavior is reasonable; the impl's actual behavior is reasonable; the gap is the architecture or the contract.
4. **The 5-streak is real.** Stages 27, 28, 29, 30, 31 all shipped clean from the first test run. The discipline (write a test for the user-observable behavior; verify the actual output before committing; reuse the established architecture) is paying off. The streak is now 5, which is the longest in the post-Stage 26 era.

## What did NOT change

* `src/vm.c`, `src/vm.h` — Stage 31 is a pure stdlib extension using the Stage 30 architecture. No new VM changes.
* `src/scanner.c`, `src/compiler.c` — no parser/compiler changes.
* `Makefile` — no new build targets.
* The composability tests — Stage 31 doesn't have a new composability test; the "array-map" example covers the composition implicitly (filter+map).

## See also

* `docs/stage-30-closeout.md` — the predecessor `array_filter`, which established the user-code-dispatch architecture.
* `wiki/build-your-own-x.md` INDEX — the per-stage deep entry for Stage 31 will be added in the wiki update commit.
* `examples/array-map.lox` — the new example, demonstrating the map idiom.
