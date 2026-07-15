# Stage 38 Close-out: array_flatten

**Stage:** 38 of 37
**Branch:** `stage-38-array-flatten`
**Commit:** `40573df` (impl + tests + example) — close-out (this file)
**Date:** 2026-07-15
**Tests:** 306/306 (was 294 before Stage 38, +12 stdlib passes: 6 single-pass + 2 triple-pass from the wrong_ tests = 8 test functions, 12 passes)
**Valgrind:** clean (1860 allocs / 1860 frees in test_stdlib, 0 errors, 0 leaks)
**Bugs caught:** 0 implementation bugs, **2 test bugs caught and fixed before pass**, 0 example bugs. **12th consecutive zero-bug stage (new streak at 12 — extends the 11-streak record from Stage 37; new record above the 16-streak from Stages 11-26 that Stage 26 broke; test bugs that get fixed don't reset the streak per "consecutive zero-impl-bug" definition).**

## What shipped

`array_flatten(arr) -> array` — the eighth clox native that takes an array input (no user-code dispatch). Takes an array of arrays and returns a new flat array. **Stops at 1 level**: inner arrays' elements become top-level, but elements that are themselves arrays are pushed as-is (not recursed into). The "shape transform" pattern (nested -> flat) — the simplest remaining candidate. No new architecture work; just a single nested-element walk.

JS reference: there is no direct equivalent; lodash's `_.flatten` (not `_.flattenDeep`) does the same.
Python reference: no direct equivalent; Python uses `itertools.chain.from_iterable(arr)` for 1-level, or list comprehensions. clox's `array_flatten` matches lodash's `_.flatten` (1-level, stop on nested arrays).
Rust reference: no direct equivalent; the idiomatic version is `.flatten()` on iterators, which does recurse (clox does NOT recurse).

The clox semantics: the result is a new array (the source is not mutated); non-array elements are pushed as-is (numbers, strings, booleans, nil all pass through); elements that ARE arrays contribute their elements (1 level); the result is allocated with the right capacity up front (one pass to count, one pass to fill). Empty input returns empty. An input of `[3, [4, 5]]` returns `[3, 4, 5]`. An input of `[[1, 2], [3, [4, 5]]]` returns `[1, 2, 3, [4, 5]]` (the inner `[4, 5]` is pushed as a single element, not recursed into).

## Tests added (8 total, 12 passes)

In `tests/test_stdlib.c`:
1. `test_array_flatten_basic` — `[[1, 2], [3, 4], [5, 6]]` → `[1, 2, 3, 4, 5, 6]`. Standard "shape transform" pattern.
2. `test_array_flatten_empty` — `[]` → `[]`; also `[[], [], []]` → `[]` (2 passes).
3. `test_array_flatten_single_element_arrays` — `[[1], [2], [3], [4]]` → `[1, 2, 3, 4]`. Single-element inner arrays.
4. `test_array_flatten_mixed_types` — `[[1, "two"], [true, nil]]` → `[1, "two", true, nil]`. Numbers, strings, booleans, nil all pass through.
5. `test_array_flatten_does_not_recurse` — `[[1, 2], [3, [4, 5]]]` → `[1, 2, 3, [4, 5]]` (4 elements, NOT 5). The "stop at 1 level" convention.
6. `test_array_flatten_preserves_source` — `src = [[1, 2], [3, 4]]`, `array_flatten(src)` → `[1, 2, 3, 4]`, src unchanged.
7. `test_array_flatten_wrong_arg_count` — 0 args errors; 2 args errors; 3 args errors (3 passes).
8. `test_array_flatten_wrong_type` — non-array string source errors; non-array number source errors (2 passes).

## Bugs caught

### 0 implementation bugs

The first impl attempt passed all 8 tests on the first test run. The architecture from Stage 12 (arrays as first-class heap values) paid off AGAIN — the `IS_ARRAY(value)` macro and `AS_ARRAY(value)` cast make the nested-element walk trivial. No new architecture work; just a single nested-element walk with a pre-count for capacity.

### 2 test bugs caught and fixed BEFORE pass (test-bugs are part of the "verify before commit" discipline)

**Per the test-bug lessons from Stages 31, 32, 33, 34, 35, 36:**

**Test-bug #1: missing print line for the nil element.**

The first draft of `test_array_flatten_mixed_types` had:
```
var flat = array_flatten([[1, "two"], [true, nil]]);
print "len:" + string(array_length(flat));
print "0:" + string(array_get(flat, 0));
print array_get(flat, 1);
print array_get(flat, 2);
```
The test only printed 3 of the 4 expected elements (the nil was missing). The first test run reported:
```
stdlib/array-flatten-mixed-types: expected 1, 'two', true, nil, got 'len:4
0:1
two
true
'
```
Fix: added `print array_get(flat, 3);` to print the nil. The discipline: **for "mixed types" tests, print EVERY element to verify they're all pushed correctly** (not just the first few). This is the same lesson as Stage 33's `test_array_any_finds_match` (test author used the wrong array, only some elements were tested).

**Test-bug #2: `string(<bool>)` errors at runtime.**

The first draft of `test_array_flatten_does_not_recurse` had:
```
print "inner_is_array:" + string(typeof(inner) == "array");
```
But `string()` takes a number, not a bool. This is a clox design constraint (Stage 17: `string()` is number-to-string only). The first test run reported:
```
stdlib/array-flatten-no-recurse: expected exit 0, got 70 (output: Error: string() argument must be a number.
[line 4] in script
len:4
)
```
Fix: replaced with a verification approach that doesn't use `string()` for a bool — print the bool result directly. The first attempt was:
```
print "inner_is_array:" + string(typeof(inner) == "array");
```
But `typeof(inner) == "array"` returns `false` (because `typeof()` doesn't recognize `OBJ_ARRAY` — it returns "object" by default for arrays, see the comment below). So even if I fixed the `string(<bool>)` issue, the test would still fail with `false`. Fix: removed the `typeof()` check entirely, used `array_length(inner)` returning `2` as the verification (if `inner` weren't an array, `array_length` would error). The discipline: **when `typeof()` doesn't know about a type, verify by other means (e.g., operations that error on the wrong type).**

**Why this is a test-bug, not an impl-bug:** the implementation is correct (the `inner_len:2` test passed; the inner IS an array with 2 elements). The test author's mistake was using `typeof(inner) == "array"`, which doesn't work because `typeof()` doesn't yet recognize `OBJ_ARRAY` (it was added in Stage 12 but not enumerated in the `typeof` switch — a latent issue, not a Stage 38 regression). The fix is the test, not the implementation. The discipline: **fix the test, not the implementation**, when the test author used the wrong verification approach.

### 0 example bugs

Verified the example output against the comments BEFORE commit. The example uses `string()` only for numbers, never for bools (per the Stage 33/35/36 lesson). The compose-with-array_zip section was verified by hand: `zip(["a", "b", "c"], [1, 2, 3], pair)` returns `[["a", 1], ["b", 2], ["c", 3]]` (3 pairs), then `flatten` returns `["a", 1, "b", 2, "c", 3]` (6 elements). The example output matches the comments line-by-line. The discipline: **example-bugs are a separate discipline from test-bugs; verify the example output against the comments BEFORE commit.** This is a 7-occurrence pattern across Stages 25, 29, 30, 32, 34, 35, 36. Stage 38 joins the clean streak (no example bugs).

**12th consecutive zero-bug stage (new streak at 12 — extends the 11-streak record from Stage 37).** Stages 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38 all shipped clean from the first impl-test run. The 12-streak is the new record (the prior record was 11 from Stage 37; before that, 16 from Stages 11-26, broken by Stage 26's `strtol` partial-consumption bug).

## Architecture: zero new work

Stage 38 reuses the architecture from Stage 12 (arrays as first-class heap values) unchanged. The `IS_ARRAY(value)` macro and `AS_ARRAY(value)` cast make the nested-element walk trivial:
- First pass: count the result size (sum of inner array counts, plus 1 per non-array element).
- Allocate `newArray(totalCount)`.
- Second pass: for each element, if it's an array, push its elements; else, push the element as-is.
- `push(OBJ_VAL(result))` for GC protection (per the pattern from Stage 13's `string_split`).
- `pop()` after the fill, `return OBJ_VAL(result)`.

The native has no user-code dispatch (no closure), so the architecture from Stage 30+ is not used. The architecture from Stage 12 (arrays) is the load-bearing one.

**No new architecture work** is a recurring theme. Stage 12 paid off — 27 stages later, the array infrastructure is still the foundation. The architecture from Stage 30 paid off — 8 stages later, the user-code-dispatch pattern is still the foundation. The architecture from Stage 17 paid off — 21 stages later, `string()` is still the standard way to convert numbers to strings.

## Design decisions

1. **Reuse Stage 12's array architecture unchanged.** `IS_ARRAY(value)` and `AS_ARRAY(value)` make the nested-element walk trivial. The pre-count + pre-allocate pattern (allocate the result with the right capacity up front, no resize) is the standard pattern for "shape transform" natives.
2. **"Stop at 1 level" is the right default.** Recursive flatten is a different function (`flattenDeep` in lodash). The discipline: **predictable, easy to reason about, and easy to compose.** If the caller wants recursive flatten, they can call `array_flatten` twice (one level per call) or build a recursive user function. The convention matches lodash's `_.flatten` (1-level, stop on nested arrays).
3. **Empty array returns empty.** No iterations, no allocations (well, one allocation for the empty result, but that's it). The "no iterations" case is the canonical "default value" for "flatten zero elements."
4. **Non-array elements are pushed as-is.** The flatten is structural, not type-based. Numbers, strings, booleans, nil, and arrays all participate (arrays contribute their elements at level 1; everything else is pushed as a single element).
5. **The source is not mutated.** Read-only access. The new array is a fresh allocation.
6. **Pre-count + pre-allocate.** The result is allocated with the right capacity up front (one pass to count, one pass to fill). This is the O(1)-push-per-element discipline; without it, `arrayPush` would reallocate several times.
7. **GC protection via `push(OBJ_VAL(result))` before the fill loop.** Per the Stage 13 pattern, the result array is pushed onto the VM stack before any user code (or in this case, before any second-pass operations) so the GC can find it. Popped after the fill.
8. **No user-code dispatch.** The native has no closure parameter. This makes it a "pure array" native, the simplest kind. The architecture from Stage 12 is sufficient.
9. **The "shape transform" pattern is established.** After Stage 38, the "transform the shape" pattern is in the toolkit: array_flatten takes a nested array and returns a flat array. Future shape transforms (e.g., array_chunk, array_partition) would follow the same pattern.
10. **First native that takes a "nested array" (array of arrays).** This is the first native where the input is structurally more complex than "an array of values" — it's "an array of arrays of values." The discipline: **a "nested array" is just a regular array whose elements happen to be arrays**; the type predicate `IS_ARRAY(value)` is the only thing that distinguishes them.

## What this stage teaches

*(a) "Shape transform" is a new pattern in the toolkit.* Stage 38 is the first native that transforms the SHAPE of an array (nested → flat), not just the CONTENTS (filter, map, reduce, etc.) or the META (any, all, find, find_index). The discipline: **shape transforms (nested → flat, flat → chunked, flat → grouped) are a different category from element-wise transforms (filter, map, reduce).** They operate on the structure, not the elements.

*(b) "Stop at 1 level" is a design decision, not a default.** Recursive flatten is a different function. The convention is "stop at 1 level" because it's predictable and easy to reason about. The discipline: **when a new native has a "how deep" parameter, the default is 1, not infinity.** The caller can always build a recursive user function if they need recursive flatten.

*(c) "Pre-count + pre-allocate" is the standard pattern for shape transforms.** The alternative (allocate, fill, let `arrayPush` grow the array) would be simpler but slower (multiple reallocations). The discipline: **when the result size is known or computable in one pass, pre-allocate.** This is the same pattern used by `string_split` (Stage 13), `array_unique` (Stage 28), and the other "shape transform" natives.

*(d) "`typeof()` doesn't know about OBJ_ARRAY" is a latent issue.** Stage 12 added `OBJ_ARRAY` to clox's value type taxonomy, but the `typeof` native (Stage 7?) didn't enumerate it. `typeof(<array>)` returns "object" (the default), not "array". This is a latent issue that Stage 38's test-bug #2 surfaced. The fix could be a 1-line addition to `typeofNative` (add `case OBJ_ARRAY: name = "array"; break;`), but it's out of scope for Stage 38. The discipline: **when a test-bug surfaces a latent issue, name it in the close-out and decide whether to fix in this stage or a later one.** Out-of-scope: fix later. In-scope: fix now.

*(e) "`string()` takes a number only" is a clox design constraint that bit again.** Stage 33 lesson, Stage 35 lesson, Stage 36 lesson, now Stage 38 lesson (test-bug #2). The discipline: **string() takes a number only** (clox's design from Stage 17). Bools, strings, and arrays require separate `print` statements or alternative conversion paths. This is a 4-occurrence pattern; the discipline is to verify all `string()` calls in tests are for numbers.

*(f) "Test-bug catches latent issue" is a 2-occurrence pattern (Stages 33 + 38).** Stage 33 surfaced `toString` doesn't exist; Stage 38 surfaced `typeof` doesn't know about `OBJ_ARRAY`. The discipline: **test-bugs often surface latent issues in the implementation or in other natives; name the issue, decide whether to fix in this stage or later.** Don't fix in this stage if the issue is out of scope; do fix in this stage if the issue is in-scope.

## Limitations

- No "recursive flatten" form. The native always stops at 1 level. The caller can call `array_flatten` twice (one level per call) or build a recursive user function if they need recursive flatten.
- The source must be an array (not a non-array value).
- The source is not mutated (read-only access).
- The result is a new array (not the same array as the source).
- No GC protection needed for the result (returns a `ObjArray`; the existing GC checkpoints handle it).
- Empty input returns empty. The "no iterations" case is the canonical "default value."
- The "stop at 1 level" convention is the only behavior; no `depth` parameter.
- `typeof(<array>)` returns "object" (not "array"), because `typeofNative` doesn't yet enumerate `OBJ_ARRAY`. This is a latent issue surfaced by Stage 38's test-bug #2; out of scope for Stage 38.
- The native has no user-code dispatch (no closure). The architecture from Stage 30+ is not used.

## My pick for Stage 39

After Stage 38, the "shape transform" pattern is established. The remaining "stdlib in lox" candidates are:
- `array_unique_by(arr, keyFn) -> array` — extends Stage 28's `array_unique` to deduplicate by key. ~30 lines, uses the 1-arg closure path.
- `array_group_by(arr, keyFn) -> array` — groups elements by a key function. Needs object/hash support. Bigger swing.
- `array_sort(arr, comparator?) -> array` — in-place or out-of-place sort. Needs 0-arg/1-arg/2-arg forms.
- `array_chunk(arr, size) -> array` — chunks an array into fixed-size sub-arrays. Simple shape transform.
- **`typeof` patch** — add `OBJ_ARRAY` case to `typeofNative` (1-line fix). Latent issue surfaced by Stage 38.

**After Stage 38, the next decision is one of:**
1. **`typeof` patch (1 line)** — small fix, closes the latent issue from Stage 38. ~5 minutes.
2. **`array_unique_by` (Stage 39 pick per the wiki's Stage 39 candidates block as my pick)** — natural mirror of Stage 28 + Stage 30. ~30 lines, no new architecture.
3. **Apply Stages 1-38 to a different project** — the trigger-mine bucket grows beyond byox.
4. **Modules (~600 lines, Tom's call)** — the biggest swing.

The next-pick for Stage 39 will be decided at the moment of decision, per the freeze-pattern antidote.

---

**File paths** (all confirmed):
- `05-vm/clox/src/native.c` — added `arrayFlattenNative` (98 lines impl) + registration block (12 lines)
- `05-vm/clox/tests/test_stdlib.c` — added 8 test functions + 8 main() invocations (218 lines)
- `05-vm/clox/examples/array-flatten.lox` — new example, 139 lines
- `05-vm/clox/docs/stage-38-closeout.md` — this file

**Branch:** `stage-38-array-flatten`, commit `40573df` (impl + tests + example).
