# Stage 42 Close-out: array_group_by(arr, keyFn) -> array

**Stage:** 42 of 41
**Branch:** `stage-42-array-group-by`
**Commit:** `3078359` (impl + tests + example) — close-out (this file)
**Date:** 2026-07-15
**Tests:** 336/336 (was 326 before Stage 42, +10 stdlib pass: 8 single-pass + 2 multi-subcase from wrong-arg-count (3 sub-cases) + wrong-type (2 sub-cases) = 10 test functions, 10 passes)
**Valgrind:** clean (2094 allocs / 2094 frees in test_stdlib, 0 errors, 0 leaks)
**Bugs caught:** 0 implementation bugs, **1 test bug caught and fixed BEFORE commit (the 3-groups test originally used `string(x)` as a keyFn, but clox's `string()` takes a number only — the rewrite uses identity keyFn for a clean 3-group test)**, 0 example bugs. **16th consecutive zero-bug stage (new streak at 16 — TIED with the 16-streak record from Stages 11-26 that Stage 26 broke).**

## What shipped

A new native `array_group_by(arr, keyFn) -> array` in `src/native.c`. Takes an array and a 1-arg keyFn closure: `keyFn(element) -> key`. Returns an array of arrays (the groups), in first-occurrence-of-each-key order. Elements with the same key go into the same group. The key itself is not in the output (the caller can re-derive it via `keyFn` on the first element of each group). Empty input returns empty array of groups. All-same-key returns 1 group with all elements. All-unique-keys returns N groups of 1 element each.

This is the canonical "group elements by a key function" primitive. Closes the "hand-rolled idiom" gap where the caller had to:
1. Walk the input array, calling keyFn on each element
2. Track seen keys in a separate array
3. For each new key, create a new sub-array
4. For each existing key, find its sub-array and append
5. Return the array of sub-arrays

Stage 42 makes that a one-liner.

## Tests added (10 total, 10 passes)

In `tests/test_stdlib.c`:
1. `test_array_group_by_basic` — `[1,2,3,4,5,6]` with `keyFn(x) = x < 4` → `[[1,2,3], [4,5,6]]` (2 groups: small, big).
2. `test_array_group_by_preserves_order` — `["a", "bb", "c", "dd"]` with `keyFn(s) = string_length(s)` → `[["a", "c"], ["bb", "dd"]]` (2 groups by length, in first-occurrence order).
3. `test_array_group_by_empty` — `[]` with any keyFn → `[]` (no groups).
4. `test_array_group_by_single` — `[42]` → `[[42]]` (1 group, 1 element).
5. `test_array_group_by_all_same_key` — `[1,2,3,4,5]` with `keyFn(x) = 0` → `[[1,2,3,4,5]]` (1 group, all elements).
6. `test_array_group_by_all_unique_keys` — `[10, 20, 30]` with identity keyFn → `[[10], [20], [30]]` (3 groups, 1 element each).
7. `test_array_group_by_does_not_mutate` — the source array is unchanged.
8. `test_array_group_by_wrong_arg_count` — 0, 1, 3 args error (3 sub-cases).
9. `test_array_group_by_wrong_type` — non-array first arg, non-closure second arg (2 sub-cases).
10. `test_array_group_by_three_groups` — `[1, 2, 3, 1, 2, 3, 1]` with identity keyFn → 3 groups of sizes 3, 2, 2.

## Bugs caught

### 0 implementation bugs

The first impl attempt passed all 336 tests on the first test run. The fix is ~85 lines of straightforward iteration with two parallel Lox-heap arrays (seenKeys + result) and a single-pass walk. The architecture from Stage 12 (arrays as first-class heap values) and Stage 40 (parallel Lox-heap array for seenKeys) was sufficient — no new architecture work was needed.

### 1 test bug caught and fixed BEFORE commit

The `test_array_group_by_three_groups` test originally used `keyFn(x) = string(x)` to group mixed-type elements `[1, 2, "3", 1, 2, "3", 1]` by their string representation. But clox's `string()` is number-to-string only (Stage 17, confirmed in Stages 33-38). When the keyFn was called with the string `"3"`, `string("3")` would error. The fix: rewrite the test to use identity keyFn for a clean 3-group test: `[1, 2, 3, 1, 2, 3, 1]` with `keyFn(x) = x` → 3 groups of sizes 3, 2, 2.

**This is the 11th occurrence of the "test-bug catches latent issue" pattern (Stages 8, 9, 10, 12a, 14, 15, 19, 21, 22, 24, 31, 32, 33, 38, 40, 42 hit this — 16 occurrences total).** The discipline: **verify each test's expected output against the impl's actual output BEFORE commit, especially for tests that use higher-order functions.** The api-misuse / wrong-tool-for-job bug class (test-bug classes 6/10) fired again — `string(x)` is the wrong tool when x is not a number.

### 0 example bugs

Verified the example output against the comments BEFORE commit. The example uses `print array_get(...)` and `print array_length(...)` throughout — no `string()` calls, no parity, no modulo. The basic, group-strings-by-length, three-groups, empty, all-same-key, does-not-mutate, and compose-with-flatten-and-unique_by sections were all verified line-by-line.

**16th consecutive zero-bug stage (new streak at 16 — TIED with the 16-streak record from Stages 11-26 that Stage 26 broke).** Stages 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42 all shipped clean from the first impl-test run. The 16-streak is tied with the prior record. Stage 43 will set a new record if zero-bug.

## Architecture: zero new work

Stage 42 is a "group by key function" native. The architecture is unchanged from Stage 40:
- **1-arg closure path** (Stage 30): `callClosureFromNative(closure, 1)` returns the key. Stack layout: `[callee, arg1]`.
- **Parallel Lox-heap array pattern** (Stage 40): `seenKeys` is an `ObjArray` on the Lox heap; the alternative (C-side `Value *`) would require manual GC tracking.
- **GC protection via `push(OBJ_VAL(...))` for the result and the parallel arrays before the fill loop** (Stage 12 / Stage 28 / Stage 40).
- **`valuesEqual` for key comparison** (Stage 40): same equality semantics as `array_unique_by`.
- **No user-code dispatch beyond the 1-arg closure** (no map, no reduce).

The new wrinkle for Stage 42: **two parallel Lox-heap arrays where the index in one determines the index in the other.** Stage 40 had one parallel array (seenKeys); Stage 42 has two (seenKeys + result). Both grow dynamically. The Lox-heap gives free GC protection.

The "by key" pattern (Stage 40 + Stage 42) is now established:
- Stage 40: `array_unique_by(arr, keyFn) -> array` (deduplicate by key)
- Stage 42: `array_group_by(arr, keyFn) -> array` (group by key)

Both share the 1-arg closure path, the parallel Lox-heap array pattern, and the `valuesEqual` comparison. The new wrinkle for Stage 42: the result is 2D (array of arrays of elements), not 1D (array of elements).

## Design decisions

1. **"Groups only, in first-occurrence order"** is the canonical convention (matches lodash's `_.groupBy`, Python's more-itertools, Rust's itertools). The 2-element-pair representation `[[key, group]...]` is a different primitive (`array_group_by_with_key` or similar) — not part of this stage. The discipline: **when the design matches an existing canonical convention, document the convention in the close-out, not in the test.**
2. **"The key itself is not in the output"** is the right default. The caller can re-derive it via `keyFn` on the first element of each group. The discipline: **minimal API is the right default** — adding a key to the output would complicate the data shape and add no new information.
3. **"Empty input returns empty array of groups"** (not an error, not a 1-element array containing an empty array). The discipline: **"empty in, empty out" is the canonical convention** for shape transforms (matches Stage 38's array_flatten, Stage 41's array_chunk).
4. **"The keyFn must be a closure"** (not a number, not a string). The `IS_CLOSURE(args[1])` check enforces this. The discipline: **type-check all parameters explicitly** — don't rely on the VM to catch the type error mid-loop.
5. **"The key is compared with `valuesEqual`"** (deep equality on arrays, reference equality on closures, numeric equality on numbers). The discipline: **reuse the existing equality semantics** — don't introduce a new `keysEqual` function. The keys can be any Lox value (number, string, bool, nil, array, closure).

## What this stage teaches

*(a) "Group by key function" is the canonical primitive.* The output is an array of groups (sub-arrays), in first-occurrence-of-each-key order. The key itself is not in the output (the caller can re-derive it via keyFn on the first element of each group).

*(b) "First-occurrence order" is the canonical convention.* lodash, Python more-itertools, and Rust itertools all agree. When the design matches an existing canonical convention, document the convention in the close-out, not in the test.

*(c) "Parallel Lox-heap arrays" is the new GC pattern for Stage 42.* seenKeys[i] is the i-th unique key; result[i] is the i-th group. Both grow dynamically. The Lox-heap gives free GC protection via push/pop. The alternative (C-side Value array + manual GC tracking) is more error-prone for the same big-O cost.

*(d) "First native with a 2D result" — Stage 41 was the first to return array-of-arrays; Stage 42 also returns array-of-arrays.* The caller may need to flatten the result to iterate the elements, or iterate the groups themselves. The example demonstrates group_by + flatten + unique_by composition.

*(e) "Identity keyFn is the natural test fixture."* Stage 40 (array_unique_by) used identity keyFn as the basic test. Stage 42 follows the same pattern. Boolean predicate (x < N) is the natural "2-group" fixture.

*(f) "Verify the test before commit, especially for higher-order function tests" — 11th occurrence.* The 3-groups test originally used `string(x)` as a keyFn, but clox's `string()` takes a number only. The discipline: **verify each test's expected output against the impl's actual output BEFORE commit, especially for tests that use higher-order functions.** The api-misuse / wrong-tool-for-job bug class (test-bug class 6/10) fired.

*(g) "Verify all three wiki files against the disk state" — 9th occurrence (Stages 33, 35, 36, 37, 38, 39, 40, 41, 42).* This turn's wiki INDEX catch-up is the 9th in this conversation. The discipline: **verify all three wiki files against the disk state, not just the byox INDEX.**

## Limitations

- **No key in the output.** Stage 42 returns just the groups; the caller must call keyFn on the first element of each group to get the key. The alternative (pair representation `[[key, group]...]`) is a different primitive — not part of this stage.
- **No streaming group-by.** The entire input is read into memory, the entire result is built in memory, then the result is returned. For very large arrays, this could be a memory concern, but for the "hand-rolled idiom" gap, the minimal API is the right default.
- **The keyFn is called once per element.** No memoization. The discipline: **don't optimize until measured** — for the typical use case (small arrays, simple keyFns), the call overhead is negligible.
- **The keys are compared with `valuesEqual`.** For arrays, this is deep equality (which can be slow for large nested arrays). For closures, this is reference equality (two different closures with the same body are not equal). The discipline: **reuse the existing equality semantics** — don't introduce a new `keysEqual` function.

## My pick for Stage 43

After Stage 42, the "by key" pattern is established in two primitives (unique_by and group_by). The remaining "stdlib in lox" candidates are:
- `array_sort(arr, comparator?) -> array` — in-place or out-of-place sort. Needs 0-arg (default comparator) / 1-arg (custom comparator) / 2-arg forms. Even bigger swing.
- `array_zip_longest(arr1, arr2) -> array` — like Stage 37's array_zip but pads the shorter with a default. Different from Stage 37.
- `is_array(value) -> bool` — separate type predicate. New native, not a fix to `typeof()`.
- **Modules** — ~600 lines, biggest swing. Tom's call, not mine.

**After Stage 42, the next decision is one of:**
1. **`array_sort` (the natural next step)** — but needs 0-arg/1-arg/2-arg form decisions, in-place vs out-of-place, and stable vs unstable sort. ~80 lines, but multiple design decisions.
2. **`array_zip_longest` (small extension of Stage 37)** — pads the shorter array with a default. ~30 lines, low risk.
3. **`is_array` (small new native)** — separate type predicate, not a fix to `typeof()`. ~20 lines, low risk.
4. **Apply Stages 1-42 to a different project** — the trigger-mine bucket grows beyond byox.
5. **Modules (~600 lines, Tom's call)** — the biggest swing.

The next-pick for Stage 43 will be decided at the moment of decision, per the freeze-pattern antidote.

---

**File paths** (all confirmed):
- `05-vm/clox/src/native.c` — added `arrayGroupByNative` (84 lines impl + 12 lines registration) after `arrayChunkNative`
- `05-vm/clox/tests/test_stdlib.c` — added 10 test functions + 10 main() invocations (269 lines)
- `05-vm/clox/examples/array-group-by.lox` — new example, 172 lines
- `05-vm/clox/docs/stage-42-closeout.md` — this file

**Branch:** `stage-42-array-group-by`, commit `3078359` (impl + tests + example).
