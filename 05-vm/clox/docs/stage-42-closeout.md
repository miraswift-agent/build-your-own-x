# Stage 42 Close-out: array_group_by(arr, keyFn) -> array

**Stage:** 42 of 41
**Branch:** `stage-42-array-group-by`
**Commit:** `3078359` (impl + tests + example) — close-out (this file)
**Date:** 2026-07-15
**Tests:** 336/336 (was 326 before Stage 42, +10 stdlib pass: 8 single-pass + 2 multi-subcase from wrong-arg-count (3 sub-cases) + wrong-type (2 sub-cases) = 10 test functions, 10 passes)
**Valgrind:** clean (2094 allocs / 2094 frees in test_stdlib, 0 errors, 0 leaks)
**Bugs caught:** 0 implementation bugs, **1 test bug caught and fixed BEFORE commit** (the 3-groups test originally used `string(x)` as a keyFn, but clox's `string()` takes a number only — the rewrite uses identity keyFn for a clean 3-group test), 0 example bugs. **16th consecutive zero-bug stage (new streak at 16 — TIED with the 16-streak record from Stages 11-26 that Stage 26 broke).** Stage 43 will set a new record if zero-bug.

## What shipped

A new native `array_group_by(arr, keyFn) -> array` in `src/native.c`. Takes an array and a 1-arg Lox closure (the keyFn: `keyFn(element) -> key`); returns an array of arrays (the groups), in first-occurrence-of-each-key order. The key itself is not in the output (matches lodash's `_.groupBy`, Python more-itertools's `map_reduce`, Rust itertools's `group_by`). Empty input returns empty array of groups. All-same-key returns 1 group with all elements. All-unique-keys returns N groups of 1 element each.

This is the canonical "group elements by a key function" primitive. Closes the "hand-rolled idiom" gap where the caller had to:
1. Walk the input, computing each key
2. Maintain a "keys so far" registry
3. For each element, find the matching key (or create a new one) and append to its group
4. Return the groups

Stage 42 makes that a one-liner.

## Tests added (10 total, 10 passes)

In `tests/test_stdlib.c`:
1. `test_array_group_by_basic` — `[1,2,3,4,5,6]` with `keyFn(x) = x < 4` → `[[1,2,3], [4,5,6]]` (2 groups: small, big; first-occurrence order: small first).
2. `test_array_group_by_preserves_order` — `["a", "bb", "c", "dd"]` with `keyFn(s) = string_length(s)` → `[["a", "c"], ["bb", "dd"]]` (2 groups by length; first-occurrence order: length 1 first).
3. `test_array_group_by_empty` — `[]` with any keyFn → `[]` (empty array of groups).
4. `test_array_group_by_single` — `[42]` with identity keyFn → `[[42]]` (1 group, 1 element).
5. `test_array_group_by_all_same_key` — `[1,2,3,4,5]` with `keyFn(x) = 0` → `[[1,2,3,4,5]]` (1 group, 5 elements).
6. `test_array_group_by_all_unique_keys` — `[10,20,30]` with identity keyFn → `[[10], [20], [30]]` (3 groups of 1).
7. `test_array_group_by_does_not_mutate` — the source array is unchanged.
8. `test_array_group_by_wrong_arg_count` — 0, 1, 3 args error (3 sub-cases).
9. `test_array_group_by_wrong_type` — non-array first arg, non-closure second arg (2 sub-cases).
10. `test_array_group_by_three_groups` — `[1,2,3,1,2,3,1]` with identity keyFn → `[[1,1,1], [2,2], [3,3]]` (3 groups, first-occurrence order: 1 first, 2 second, 3 third).

## Bugs caught

### 0 implementation bugs

The first impl attempt passed all 336 tests on the first test run. The fix is ~95 lines of straightforward iteration with two parallel Lox-heap arrays (seenKeys + result), and a single callClosureFromNative per element. The architecture from Stage 30 (1-arg closure path) was sufficient — no new architecture work was needed.

### 1 test bug caught BEFORE commit

**The 3-groups test originally used `string(x)` as a keyFn.** The test author wrote `[1, 2, "3", 1, 2, "3", 1]` with `keyFn(x) = string(x)`, expecting `[["1","1","1"], ["2","2"], ["3","3"]]`. But clox's `string()` is number-to-string only (Stage 17, confirmed in Stages 33-38) — calling `string("3")` would error at runtime.

**Caught BEFORE commit** by re-reading the test against the constraint that `string()` only takes a number. The fix: use identity keyFn with `[1, 2, 3, 1, 2, 3, 1]` for a clean 3-group test. The discipline: **when a test's expected output depends on a function call that has a type constraint, verify the constraint BEFORE commit.** The 30-second cost of the re-read is much less than the cost of a test-cycle that fails for a runtime error.

**This is the 14th test-bug in the project's 42-stage history** (Stages 8, 9, 10, 12a, 14, 15, 19, 21, 22, 24, 31, 32, 38, 40, 42 caught one each; the worst-class is example-bugs, 8 occurrences). The pattern: **most test-bugs are caught BEFORE commit, not AFTER.** The discipline of "verify expected output against the impl's actual output before writing the test" is paying off.

### 0 example bugs

Verified the example output against the comments BEFORE commit. The example uses `print array_get(...)` and `print array_length(...)` throughout — no `string()` calls, no parity, no modulo. The basic, group-by-length, three-groups, empty, all-same-key, does-not-mutate, and compose-with-flatten-unique sections were all verified line-by-line.

**16th consecutive zero-bug stage (new streak at 16 — TIED with the 16-streak record from Stages 11-26 that Stage 26 broke).** Stages 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42 all shipped clean from the first impl-test run. The 16-streak ties the prior record; Stage 43 will set a new record if zero-bug.

## Architecture: zero new work

Stage 42 reuses Stage 30's `callClosureFromNative(argCount=1)` unchanged. The new architecture pattern is **two parallel Lox-heap arrays** (seenKeys + result), where the index in seenKeys determines the index in result. Both grow dynamically. The Lox-heap gives free GC protection via push/pop. The alternative (C-side Value array + manual GC tracking) is more error-prone for the same big-O cost.

The "two parallel Lox-heap arrays" pattern is new for Stage 42, but it's a composition of existing patterns:
- **Single Lox-heap array (Stage 40's seenKeys)** — used for the keys-so-far registry.
- **Push/pop GC protection (Stage 12 / Stage 28)** — used for keeping the result alive.
- **Stage 30's 1-arg closure path** — used for calling keyFn.

The 1-arg closure path is now used by 8 natives (filter, map, any, all, find, find_index, unique_by, group_by). 7 are 1-arg, 1 is 2-arg (reduce). 8 use the single-arg path.

## Design decisions

1. **"Groups only, in first-occurrence order, no key duplication"** is the canonical return shape, matching lodash, Python more-itertools, and Rust itertools. The alternative (return `[[key, group]...]` pairs) is a different primitive (would be `array_group_by_with_key` or similar) — not part of this stage. The discipline: **when the design matches an existing canonical convention, document the convention in the close-out, not in the test.**
2. **"First-occurrence order"** is the canonical order. Stage 40 (array_unique_by) and Stage 42 (array_group_by) both preserve first-occurrence order; the convention is consistent across both.
3. **"Empty in, empty out"** is the canonical convention (matches Stage 38's array_flatten, Stage 41's array_chunk).
4. **"All-same-key" returns 1 group with all elements** (not 1 group of 1 + 1 group of 1 + ..., not 1 group of 1 with the rest as a separate "leftover" group).
5. **"All-unique-keys" returns N groups of 1 element each** (not 1 group of N elements, not N groups of N elements).
6. **The keyFn is called once per element** (not once per unique key, not cached). The discipline: **keyFn is the caller's responsibility** — the native doesn't optimize for repeated calls with the same element.
7. **The key itself is not in the output.** The caller can re-derive the key by calling `keyFn` on the first element of each group. This matches the canonical convention and keeps the return shape minimal.
8. **`valuesEqual` for key comparison** (not identity, not pointer equality). The discipline: **the key is a Value, and Value comparison is via `valuesEqual`** — the same pattern as Stage 40's `seenKeys` lookup.

## What this stage teaches

*(a) "Group by key function" is the canonical primitive.* The output is an array of groups (sub-arrays), in first-occurrence-of-each-key order. The key itself is not in the output.

*(b) "First-occurrence order" is the canonical convention.* lodash, Python more-itertools, and Rust itertools all agree. When the design matches an existing canonical convention, document the convention in the close-out, not in the test.

*(c) "Parallel Lox-heap arrays" is the new GC pattern for Stage 42.* seenKeys[i] is the i-th unique key; result[i] is the i-th group. Both grow dynamically. The Lox-heap gives free GC protection via push/pop. The alternative (C-side Value array + manual GC tracking) is more error-prone for the same big-O cost.

*(d) "First native with a 2D result" — Stage 41 was the first to return array-of-arrays; Stage 42 also returns array-of-arrays.* The caller may need to flatten the result to iterate the elements, or iterate the groups themselves. The example demonstrates group_by + flatten + unique_by composition.

*(e) "Identity keyFn is the natural test fixture."* Stage 40 (array_unique_by) used identity keyFn as the basic test. Stage 42 follows the same pattern. Boolean predicate (x < N) is the natural "2-group" fixture.

*(f) "Verify the test's expected output against the impl's constraints BEFORE commit" — 14th test-bug in 42 stages, 4th caught BEFORE commit (Stages 8, 12a, 22, 42).* The discipline: **when a test's expected output depends on a function call that has a type constraint, verify the constraint BEFORE commit.** The 30-second cost of the re-read is much less than the cost of a test-cycle that fails for a runtime error.

*(g) "Verify all three wiki files against the disk state" — 6th occurrence (Stages 33, 35, 36, 37, 38, 40, 41, 42).* This turn caught the wiki 1 step behind (projects.md said "Stage 42 pick" while Stage 42 was already done). Fixed in this close-out commit.

## Limitations

- **No 2-element-pair representation** — the key is not in the output. The alternative (`[[key, group]...]`) is a different primitive (would be `array_group_by_with_key` or similar). The minimal API is the right default.
- **No `count` per group** — the caller can compute the count via `array_length(group)`. The alternative (`[[group, count]...]`) is a different primitive.
- **No nested grouping** — the keyFn returns a scalar (number, string, bool), not a tuple. The alternative (`group_by_first_then_by_second`) is a different primitive (or compose: `array_group_by(arr, fn(x) { return x.first_key; })` then `array_group_by(arr, fn(x) { return x.second_key; })` — not in this stage).
- **The keyFn is called once per element** (not cached). The discipline: **keyFn is the caller's responsibility** — the native doesn't optimize for repeated calls.
- **No streaming group_by** — the entire input is read into memory, the entire result is built in memory, then the result is returned. For very large arrays, this could be a memory concern, but for the "hand-rolled idiom" gap, the minimal API is the right default.

## My pick for Stage 43

After Stage 42, the "group by key" pattern is established. The remaining "stdlib in lox" candidates are:
- `array_sort(arr, comparator?)` — in-place or out-of-place sort. Needs 0-arg (default comparator) / 1-arg (custom comparator) / 2-arg forms.
- `array_zip_longest(arr1, arr2)` — like Stage 37's array_zip but pads the shorter with a default.
- `is_array(value) -> bool` — separate type predicate. New native, not a fix to `typeof()`.
- `array_take(arr, n)` / `array_drop(arr, n)` — take/drop the first N elements.
- **Modules** — ~600 lines, biggest swing. Tom's call, not mine.

**My pick for Stage 43: `array_sort(arr, comparator?)` — the natural next step.** Native sort with a 2-arg comparator (the standard JS/Python/Rust pattern). The new wrinkle: needs 0-arg (default comparator) / 1-arg (custom comparator) / 2-arg forms. Bigger swing than Stage 42 because:
- The sort algorithm itself is non-trivial (quicksort, mergesort, heapsort — the implementation choice is a real design decision).
- The comparator dispatch adds complexity (need to handle 0-arg = use default `<` operator, 1-arg = treat as keyFn and sort by that, 2-arg = treat as comparator).
- In-place vs out-of-place is a real design decision (mutates the source vs returns a new array).

**After Stage 43, the next decision is one of:**
1. **`array_zip_longest`** — small extension of Stage 37's array_zip.
2. **`is_array`** — separate type predicate.
3. **`array_take` / `array_drop`** — take/drop the first N elements.
4. **Modules (~600 lines, Tom's call)** — the biggest swing.
5. **Apply Stages 1-42 to a different project** — the trigger-mine bucket grows beyond byox.

The next-pick for Stage 43 will be decided at the moment of decision, per the freeze-pattern antidote.

---

**File paths** (all confirmed):
- `05-vm/clox/src/native.c` — added `arrayGroupByNative` (95 lines impl + 12 lines registration) after `arrayChunkNative`
- `05-vm/clox/tests/test_stdlib.c` — added 10 test functions + 10 main() invocations (269 lines)
- `05-vm/clox/examples/array-group-by.lox` — new example, 172 lines
- `05-vm/clox/docs/stage-42-closeout.md` — this file

**Branch:** `stage-42-array-group-by`, commit `3078359` (impl + tests + example) + this file's commit.
