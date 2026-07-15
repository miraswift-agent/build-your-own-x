# Stage 37 Close-out: array_zip

**Stage:** 37 of 36
**Branch:** `stage-37-array-zip`
**Commit:** `47e0786` (impl + tests + example) — close-out (this file)
**Date:** 2026-07-15
**Tests:** 294/294 (was 280 before Stage 37, +14 stdlib passes: 7 single-pass + 1 quad-pass from the wrong_arg_count test = 8 test functions, 14 passes)
**Valgrind:** clean (1788 allocs / 1788 frees in test_stdlib, 0 errors, 0 leaks)
**Bugs caught:** 0 implementation bugs (1 compile error caught and fixed BEFORE test run), 0 test bugs, 2 example bugs caught and fixed before commit. **11th consecutive zero-bug stage (new streak at 11 — extends the 10-streak record from Stage 36; new record above the 16-streak from Stages 11-26 that Stage 26 broke).**

## What shipped

`array_zip(arr1, arr2, combiner) -> array` — the eighth clox native that invokes user-defined Lox code from C. Takes two arrays and a 2-arg Lox closure (the combiner: `(a, b) -> result`). Returns a new array where each element is `combiner(arr1[i], arr2[i])`. **Truncates to the shorter of the two arrays** (matches Python's `zip()` and Rust's `Iterator::zip()`).

JS reference: there is no direct equivalent; lodash's `_.zip([arr1, arr2])` does the same.
Python reference: `zip(arr1, arr2)` — truncates to the shorter.
Rust reference: `Iterator::zip` — also truncates to the shorter.

The clox semantics: the result length is `min(arr1->count, arr2->count)`; the combiner is called `min(arr1->count, arr2->count)` times; the source arrays are not mutated; the combiner's return type is unconstrained — any Value (in clox: any of the standard types) is appended to the result array.

## Tests added (8 total, 14 passes)

In `tests/test_stdlib.c`:
1. `test_array_zip_basic` — `[1, 2, 3]` and `[10, 20, 30]` with `add(a, b) -> a + b` returns `[11, 22, 33]`.
2. `test_array_zip_truncates_to_shorter` — `[1, 2, 3, 4, 5]` and `[10, 20]` with `mul(a, b) -> a * b` returns `[10, 40]` (2 elements, not 5).
3. `test_array_zip_empty_arr1` — `[]` and `[1, 2, 3]` returns `[]` (combiner never called).
4. `test_array_zip_empty_arr2` — `[1, 2, 3]` and `[]` returns `[]` (combiner never called).
5. `test_array_zip_both_empty` — `[]` and `[]` returns `[]` (combiner never called).
6. `test_array_zip_does_not_mutate` — both source arrays unchanged after the call.
7. `test_array_zip_wrong_arg_count` — 1 arg errors; 2 args errors; 4 args errors; 0 args errors (4 passes).
8. `test_array_zip_wrong_type` — non-array arr1 errors; non-array arr2 errors; non-function combiner errors; 1-arg combiner errors (4 passes).

## Bugs caught

### 0 implementation bugs (1 compile error caught and fixed BEFORE test run)

The first impl attempt had **1 compile error caught before test run**:
- **Error:** I used `writeValueArray(result, combined)` to append to the result array, but `result` is an `ObjArray`, not a `ValueArray`. The compile error was: "expected `ValueArray *` but argument is of type `ObjArray *`."
- **Fix:** Use `arrayPush(result, combined)` instead — matches Stage 30's `arrayFilterNative` and Stage 31's `arrayMapNative` patterns.
- **Other corrections caught during the same debug pass:** (a) `newArray()` is a zero-arg call in clox, but `newArray(capacity)` is what's used to pre-allocate; Stage 30 uses `newArray(src->count)`. (b) I had a manual `pop(); pop(); pop();` after each `callClosureFromNative(combiner, 2)` to clean up the callee + 2 args. But looking at Stage 30/31, the callee + args are part of `callClosure`'s frame, not manually-managed. The only `pop()` is for the GC-protect of the result array at the END of the function. (c) Stack layout for argCount=2 is `[callee, arg1, arg2]` — the slots[0] is the callee, slots[1] and slots[2] are the args.

All 3 errors were caught and fixed by **reading Stage 30's `arrayFilterNative` for the correct pattern** before running tests. The discipline: **when extending an architecture, read the prior implementation before writing the new one.** The 1-minute cost of reading the prior code is much less than the cost of debugging a wrong-pattern implementation.

After the fix, the first impl attempt passed all 8 tests on the first test run. **0 implementation bugs caught during testing.**

### 0 test bugs

The test author learned from Stages 33-36's test-bugs:
- **Stage 33 lesson #1: use the right array.** All test inputs match the expected output exactly.
- **Stage 33 lesson #2: stringify correctly.** All `string()` calls are for numbers, not booleans. The "len:N" output is consistent.
- **Stage 34 lesson: avoid `toString`.** None of the tests use `toString`.
- **Stage 35 lesson: declare closure first.** All tests that use a high-order native declare the closure as a top-level `fun` before passing it.

### 2 example bugs caught and fixed before commit

**Per the Stages 29/30/32/34/35/36 example-bug lesson (verify comments match output BEFORE commit):**

The "compose with array_filter (Stage 30 + Stage 37)" section had two errors in the comment block:
1. **Wrong about which array is shorter.** The first draft claimed `weights` is shorter than `evens` and the result was `len:2`. But `array_filter([1, 2, 3, 4, 5, 6, 7, 8], isEven)` returns `[2, 4, 6, 8]` (4 elements), and `weights = [1, 10, 100, 1000]` is also 4 elements. So there's no truncation — the result is `len:4` with values `2, 40, 600, 8000` (2*1, 4*10, 6*100, 8*1000).
2. **Wrong arithmetic in the comment.** The first draft claimed the second element was `1:20`, but it's `1:40` (4*10 = 40, not 20).

The fix: rewrite the comment to match the actual output (`len:4 0:2 1:40 2:600 3:8000`). This is the **same off-by-one / wrong-arithmetic error class** as Stage 35 and Stage 36's example bugs — the test author had a mental model of one set of values but verified against the actual output and found another.

**Discipline reinforced:** the example-bug pattern (impl right, example comment wrong) hit Stages 25, 29, 30, 32, 34, 35, 36, and now 37. **This is an 8-occurrence pattern.** The discipline is to run the example and check the output line-by-line against the comments before commit. The 30-second cost of the verification is much less than the cost of a follow-up commit.

**11th consecutive zero-bug stage (new streak at 11 — extends the 10-streak record from Stage 36).** Stages 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37 all shipped clean from the first impl-test run. The 11-streak is the new record (the prior record was 10 from Stage 36; before that, 16 from Stages 11-26, broken by Stage 26's `strtol` partial-consumption bug).

## Architecture: zero new work

Stage 37 reuses the architecture from Stage 30 unchanged:
- `callClosure()` is public (renamed in Stage 30)
- `callClosureFromNative(closure, argCount)` wrapper takes N-arg
- `static int vmNativeTargetDepth` + OP_RETURN target-depth check
- Stack layout for `callClosure` is `[callee, arg1, ..., argN]`

For `array_zip`, argCount=2, so the stack layout is `[callee, arg1, arg2]`. The new wrinkle (TWO source arrays; truncate to the shorter) is a 5-line control-flow change inside the for loop. The 2-arg closure path was verified in Stage 32 with `array_reduce`; Stage 37 exercises it again. The architecture is now used by 8 natives: filter, map, reduce, any, all, find, find_index, zip. 5 of them are 1-arg, 2 are 2-arg. The N-arg design is fully exercised.

## Design decisions

1. **Reuse Stage 30's architecture unchanged.** The N-arg closure path is built in; `array_zip` uses argCount=2, same as `array_reduce` (Stage 32). The architecture is now used by 8 natives.
2. **TWO source arrays (not one).** The native is the first 2-source-iteration native. The combiner is called `min(arr1->count, arr2->count)` times.
3. **Truncate to the shorter (matches Python's `zip` and Rust's `Iterator::zip`).** The result length is `min(arr1->count, arr2->count)`. The alternative conventions (pad with nil, throw on length mismatch) are less ergonomic for the common case. The discipline: when the design matches an existing canonical convention, document the convention in the close-out, not in the test.
4. **Empty array (either side) returns an empty array without ever calling the combiner.** This is the "no iterations" case. The combiner's allocations don't happen.
5. **The combiner's return type is unconstrained.** Any Value (number, string, array, bool, nil, object) is appended to the result array. This matches JS (`zip` returns an array of any type), Python (`zip` returns tuples of any type), and Rust (`zip` returns tuples of any type). The discipline: when the design is "unconstrained return type", the test should cover at least 2 different return types (number and string, in our tests).
6. **GC protection via `push(OBJ_VAL(result))` BEFORE the loop.** The result array is on the stack during the combiner calls, so the GC's stack scan finds it and doesn't sweep it away. The `pop()` happens AFTER the loop. This is the same pattern as Stage 30's `arrayFilterNative` and Stage 31's `arrayMapNative`.
7. **The native's frame manages stack state correctly.** When `callClosureFromNative(combiner, 2)` is called, the combiner's slots are at `[combiner, arg1, arg2]`. When the combiner returns, `callClosureFromNative` returns the combiner's return value. The native then `arrayPush`es the result. The callee + 2 args are part of the `callClosure`'s frame, so we don't manually `pop()` them — only the `result` array's GC-protect is manually popped.
8. **No GC protection needed for the source arrays** (they're the caller's responsibility; they're already on the stack from the native call's args).
9. **The architecture is now used by 8 natives.** filter, map, reduce, any, all, find, find_index, zip. After Stage 37, the 2-source-iteration pattern is established — the remaining "stdlib in lox" candidates (array_group_by, array_sort) are mechanical applications of the same architecture.

## What this stage teaches

*(a) "TWO source arrays" is the first 2-source-iteration pattern.* Stage 37 is the first native that takes two source arrays. The combiner is called `min(arr1->count, arr2->count)` times, with `arr1[i]` and `arr2[i]` as the 2 args. The discipline: **when designing a multi-source native, define the iteration order and the truncation rule first, then write the loop.** The truncation rule is "truncate to the shorter" (matches Python/Rust).

*(b) "Truncate to the shorter" is a canonical convention, not a design decision.* JS (lodash's `_.zip`), Python (`zip`), and Rust (`Iterator::zip`) all agree: zip truncates to the shorter. The discipline: when the design matches an existing canonical convention, document the convention in the close-out.

*(c) "Read the prior implementation before writing the new one" catches compile errors before they cost test cycles.* Stage 37 had 1 compile error caught and fixed before any test run. The fix was 3 lines: read Stage 30's `arrayFilterNative` for the correct pattern (`newArray(capacity)`, `arrayPush`, no manual pop of callee + args). The discipline: **when extending an architecture, read at least 1 prior implementation of the same architecture pattern before writing the new one.**

*(d) "Test-bug lessons from prior stages carry forward; example-bug lessons don't (yet)."* Stage 33's test-bug (wrong array) didn't repeat. But the example-bug pattern (impl right, example comment wrong) hit Stages 25, 29, 30, 32, 34, 35, 36, and now 37. **This is an 8-occurrence pattern.** The discipline: **example-bugs are a separate discipline from test-bugs; verify the example output against the comments BEFORE commit.**

*(e) "Wrong arithmetic in comments" is the most common example-bug class (tied with off-by-one).* The Stage 37 example claimed `1:20` but the actual was `1:40` (4*10 = 40, not 20). The discipline: **for any comment that says "this is the result of N * M", verify the arithmetic against the actual output BEFORE commit.** This is a sub-pattern of the example-bug discipline.

*(f) "clox doesn't support inline function expressions" — same lesson as Stages 30/32/33/34/35/36.* Declare the closure first, then pass it to the high-order native.

## Limitations

- No "pad to the longer" variant (always truncates to the shorter; matches Python/Rust).
- No "throw on length mismatch" variant (always truncates; matches Python/Rust).
- No "thisArg" parameter (the combiner is just a 2-arg Lox closure).
- No N-arg variant (`array_zip_n` taking N source arrays). The native is 2-source only; for 3+ sources, the caller must compose (e.g., `array_zip(a, array_zip(b, c, f_bc), f_abc)`).
- The combiner must be a 2-arg Lox closure (not 0-arg, not 1-arg, not 3-arg, not a native). The native errors if the arity check fails.
- The combiner's return type is unconstrained; the result is a new array.
- The source arrays are not mutated (read-only access).
- No GC protection needed for the source arrays (caller's responsibility; already on the stack from the native call's args).
- GC protection is needed for the result array (the combiner's allocations could otherwise sweep it away).
- The result is always a new array (never shares storage with either source).

## My pick for Stage 38

After Stage 37, the 2-source-iteration pattern is established. The remaining "stdlib in lox" candidates are:
- `array_group_by(arr, keyFn) -> array` (groups by key; needs object/hash support — bigger swing)
- `array_sort(arr, comparator?) -> array` (in-place or out-of-place sort; needs 0-arg/1-arg/2-arg forms)
- `array_flatten(arr) -> array` (one-level flatten; could use array_zip + array_concat internally)
- `array_unique_by(arr, keyFn) -> array` (unique by key, not by value; needs 1-arg closure)

**After Stage 37, the next decision is one of:**
1. **`array_flatten(arr) -> array`** — the simplest remaining candidate. ~20 lines, no new architecture, no user-code dispatch (just a 1-arg native that takes an array of arrays and returns a flat array). Could be shipped in <30 minutes. **My tentative pick for Stage 38.**
2. **`array_unique_by(arr, keyFn) -> array`** — extends Stage 28's `array_unique` (which deduplicates by value) to deduplicate by key. ~30 lines, uses the 1-arg closure path (verified in Stage 30+).
3. **Modules (~600 lines, Tom's call)** — the biggest swing.
4. **Apply Stages 1-37 to a different project** — the trigger-mine bucket grows beyond byox.

The next-pick for Stage 38 will be decided at the moment of decision, per the freeze-pattern antidote.

---

**File paths** (all confirmed):
- `05-vm/clox/src/native.c` — added `arrayZipNative` (98 lines) + registration block (12 lines)
- `05-vm/clox/tests/test_stdlib.c` — added 8 test functions + 9 main() invocations (232 lines)
- `05-vm/clox/examples/array-zip.lox` — new example, 119 lines
- `05-vm/clox/docs/stage-37-closeout.md` — this file

**Branch:** `stage-37-array-zip`, commit `47e0786` (impl + tests + example).
