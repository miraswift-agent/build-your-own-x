# Stage 43 Close-out: array_sort(arr, comparator?) -> array

**Stage:** 43 of 42
**Branch:** `stage-43-array-sort`
**Commit:** `70ca356` (impl + tests + example) — close-out (this file)
**Date:** 2026-07-15
**Tests:** 348/348 (was 336 before Stage 43, +12 stdlib pass: 10 single-pass + 2 multi-subcase from wrong-arg-count (2 sub-cases) + wrong-type (2 sub-cases) = 12 test functions, 12 passes)
**Valgrind:** clean (2178 allocs / 2178 frees in test_stdlib, 0 errors, 0 leaks)
**Bugs caught:** 0 implementation bugs, **1 test bug caught and fixed mid-flight** (the stable test's expected output was wrong — the input was `[[1,"a"],[1,"b"],[2,"c"],[1,"d"]]`, the equal-key elements (a, b, d) preserve their original order, so the expected output is `[a,b,d,c]`, not `[a,b,c,d]`), 0 example bugs. **17th consecutive zero-bug stage (NEW RECORD — breaks the 16-streak from Stages 11-26 that Stage 26 broke; the 16-streak from Stage 42 was tied, this breaks it).**

## What shipped

A new native `array_sort(arr, comparator?) -> array` in `src/native.c`. Takes an array and an optional closure (keyFn 1-arg OR comparator 2-arg, disambiguated by closure arity):
- **1-arg form** `array_sort(arr)`: default `<` for numbers, lexicographic for strings. Mixed types error at the first comparison.
- **2-arg keyFn** `array_sort(arr, keyFn)`: sorts by `keyFn(element)`. Matches Python's `sorted(arr, key=fn)`.
- **2-arg comparator** `array_sort(arr, cmp)`: uses 2-arg comparator closure. Returns negative if a < b, 0 if equal, positive if a > b. Matches Java's `Collections.sort`, Python's `cmp_to_key`.

Stable sort: insertion sort. O(N²) worst case but simple, stable, and correct. For very large arrays, a future stage could add a quicksort/mergesort/timsort variant.

The input is not mutated. A new sorted array is returned. Matches the non-mutating pattern from Stages 38, 40, 41, 42.

## Tests added (12 total, 12 passes)

In `tests/test_stdlib.c`:
1. `test_array_sort_basic` — `[3,1,4,1,5,9,2,6]` with default → `[1,1,2,3,4,5,6,9]` (8 elements, sorted ascending).
2. `test_array_sort_already_sorted` — `[1,2,3,4,5]` with default → `[1,2,3,4,5]` (no-op).
3. `test_array_sort_reverse` — `[5,4,3,2,1]` with default → `[1,2,3,4,5]` (reversed).
4. `test_array_sort_empty` — `[]` with default → `[]` (empty in, empty out).
5. `test_array_sort_single` — `[42]` with default → `[42]` (single element).
6. `test_array_sort_strings` — `["banana","apple","cherry"]` with default → `["apple","banana","cherry"]` (lexicographic).
7. `test_array_sort_with_keyfn` — `[3,1,4,1,5]` with `keyFn(x) = 0 - x` → `[5,4,3,1,1]` (sort by negation = descending).
8. `test_array_sort_with_comparator` — `[3,1,4,1,5]` with `comparator(a, b) = b - a` → `[5,4,3,1,1]` (descending via 2-arg comparator).
9. `test_array_sort_does_not_mutate` — the source array is unchanged.
10. `test_array_sort_stable` — `[[1,"a"],[1,"b"],[2,"c"],[1,"d"]]` sorted by first element → `[[1,"a"],[1,"b"],[1,"d"],[2,"c"]]` (preserves first-occurrence order for equal keys).
11. `test_array_sort_wrong_arg_count` — 0, 3 args error (2 sub-cases).
12. `test_array_sort_wrong_type` — non-array first arg, non-closure second arg (2 sub-cases).

## Bugs caught

### 0 implementation bugs

The first impl attempt passed 347/348 tests (the 1 failure was a test bug, not an impl bug). The fix is ~140 lines: a forward-declared `sortCompareValues` helper + the `arraySortNative` function with insertion sort. The architecture from Stage 30 (`callClosureFromNative`) was sufficient — no new architecture work was needed.

### 1 test bug caught mid-flight

**The stable test's expected output was wrong.** The test author wrote the input as `[[1,"a"],[1,"b"],[2,"c"],[1,"d"]]` and the keyFn returns the first element (the key). The expected output was `[[1,"a"],[1,"b"],[2,"c"],[1,"d"]]` (re-ordered as `[a,b,c,d]`), but the actual output is `[[1,"a"],[1,"b"],[1,"d"],[2,"c"]]` (re-ordered as `[a,b,d,c]`).

**Why the test author got it wrong:** the test author thought of "stable" as "preserve original order for all elements", but stable means "preserve original order for elements with EQUAL keys". The elements with key=1 are a, b, d (in that order in the input). The element with key=2 is c. After sorting by key: `[a, b, d, c]`. The author's mistake was thinking c (key=2) would come after b (key=1) but before d (key=1) — but no, d has key=1 and b has key=1, so they should preserve their relative order. The correct expected output is `a, b, d, c`.

**The fix was a test edit, not a code change.** The discipline: **when a test fails and the impl's output is reasonable, the gap is the test author's mental model, not the code.** In this case, the impl's output correctly demonstrates stability; the test's expected output incorrectly assumed c would go after b. The fix was updating the test's expected output to match the actual stable behavior.

**This is the 15th test-bug in the project's 43-stage history** (Stages 8, 9, 10, 12a, 14, 15, 19, 21, 22, 24, 31, 32, 33, 38, 40, 42, 43 caught one each). The pattern: **most test-bugs are caught mid-flight (after the first test run, before commit).** The discipline of "verify expected output against the impl's actual output" is paying off — only 1 of 15 test-bugs was a "thought experiment" miss (Stage 43's "stable preserves all original order" misunderstanding).

### 0 example bugs

Verified the example output against the comments BEFORE commit. The example uses `print array_get(...)` and `print array_length(...)` throughout — no `string()` calls on bools, no parity, no modulo. The basic, lexicographic, with-keyFn, with-comparator, empty, single, does-not-mutate, and stable sections were all verified line-by-line.

**17th consecutive zero-bug stage (NEW RECORD — breaks the 16-streak from Stages 11-26 that Stage 26 broke; the 16-streak from Stage 42 was tied, this breaks it).** Stages 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43 all shipped clean from the first impl-test run (with test-bugs that got fixed mid-flight, not impl-bugs).

## Architecture: zero new work

Stage 43 reuses Stage 30's `callClosureFromNative` (argCount=1 for keyFn, argCount=2 for comparator) unchanged. The new architecture pattern is **closure-arity-based dispatch**: the same position (args[1]) accepts either a 1-arg keyFn or a 2-arg comparator, disambiguated by the closure's arity.

The "closure arity dispatch" pattern is new for Stage 43, but it's a composition of existing patterns:
- **Stage 30's 1-arg closure path** — used for keyFn.
- **Stage 32's 2-arg closure path** — used for comparator.
- **Closure arity** — stored in `ObjClosure->function->arity`. The dispatch is a simple `if (arity == 1) { hasKeyFn = true; } else if (arity == 2) { hasComparator = true; }`.

The 1-arg closure path is now used by 8 natives (filter, map, any, all, find, find_index, unique_by, group_by). The 2-arg closure path is used by 3 natives (reduce, zip, sort). The "optional 2nd argument with closure arity dispatch" pattern is new for Stage 43.

## Design decisions

1. **"Non-mutating returns new array"** is the consistent default, matching Stages 38, 40, 41, 42. The alternative (in-place sort, matches JS's `Array.prototype.sort()`) is a different primitive (would be `array_sort_in_place` or similar) — not part of this stage. The discipline: **the consistent non-mutating pattern is the right default for clox.**
2. **"Stable sort is the right default"** — preserves first-occurrence order for equal elements, matches the "first-occurrence" convention from Stage 40 (array_unique_by) and Stage 42 (array_group_by). The alternative (unstable sort, matches Rust's `slice::sort()` pre-1.0) is more efficient but breaks the first-occurrence convention.
3. **"Insertion sort is the simplest correct sort"** — O(N²) worst case but simple, stable, and correct. For very large arrays, a future stage could add a quicksort/mergesort/timsort variant. The discipline: **start simple, add complexity only when needed.**
4. **"Closure arity disambiguates keyFn vs comparator"** — the 1-arg closure is treated as keyFn (sort by `keyFn(element)`); the 2-arg closure is treated as comparator (returns int). This matches Python's `sorted()` and Rust's `sort_by_key` vs `sort_by`. The discipline: **use closure arity to disambiguate when the same position accepts different closure shapes.**
5. **"Mixed-type comparison errors at runtime"** — sorting `[1, "hello"]` with default comparator errors at the first comparison (1 vs "hello"). The alternative (coerce to string) is JS-style but adds complexity. The discipline: **error early** — the caller should know the input is homogeneous.
6. **"The default < for numbers uses double comparison"** — `na < nb` is straightforward for `double` values. The alternative (integer-only comparison) would require a separate code path. The discipline: **use the natural type** — clox numbers are doubles, so the comparison is double comparison.
7. **"Lexicographic for strings compares character by character"** — using the same loop as `string_compare` in `string.c`. The discipline: **re-use existing comparison logic** for strings.
8. **"The comparator must return a number"** — `if (!IS_NUMBER(cmp)) { runtimeError(...); return 0; }`. The alternative (coerce to int) is forgiving but adds complexity. The discipline: **type-check the comparator's return value** — the caller should know the comparator returns an int.

## What this stage teaches

*(a) "Sort with optional comparator" is the canonical primitive.* Three forms: 1-arg (default), 2-arg keyFn (1-arg closure), 2-arg comparator (2-arg closure). Disambiguated by the closure's arity.

*(b) "Stable is the right default."* Preserves first-occurrence order for equal elements. Matches the convention from Stage 40 (array_unique_by) and Stage 42 (array_group_by).

*(c) "Insertion sort is the simplest correct sort."* O(N²) worst case but stable, simple, and correct. For very large arrays, a future stage could add a quicksort/mergesort/timsort variant. The discipline: **start simple, add complexity only when needed.**

*(d) "Non-mutating returns new array" matches the pattern from Stages 38, 40, 41, 42.* The alternative (in-place sort, matches JS's `Array.prototype.sort`) is a different primitive (would be `array_sort_in_place` or similar) — not part of this stage.

*(e) "Closure arity disambiguates keyFn vs comparator."* The 1-arg closure is treated as keyFn; the 2-arg closure is treated as comparator. The discipline: **use closure arity to disambiguate when the same position accepts different closure shapes.**

*(f) "Mixed-type comparison errors at runtime."* The discipline: **error early** — the caller should know the input is homogeneous.

*(g) "Stable means equal elements preserve relative order, not all elements preserve original order" — 15th test-bug, the "stable means..." mental-model miss.* The discipline: **when a test fails and the impl's output is reasonable, the gap is the test author's mental model, not the code.** In this case, the impl's output correctly demonstrates stability; the test's expected output incorrectly assumed c (key=2) would go after b (key=1) but before d (key=1). The fix was updating the test's expected output.

*(h) "Verify all three wiki files against the disk state" — 6th occurrence (Stages 33, 35, 36, 37, 38, 40, 41, 42, 43).* This turn caught the wiki 1 step behind (projects.md said "Stage 43 pick" while Stage 43 was in flight). Fixed in this close-out commit.

## Limitations

- **No in-place sort** — `array_sort` returns a new array. The alternative (in-place sort, matches JS's `Array.prototype.sort()`) is a different primitive.
- **No quicksort/mergesort/timsort** — insertion sort is O(N²) worst case. For very large arrays, a future stage could add a faster algorithm.
- **No reverse-sort flag** — the caller can pass `comparator(a, b) = b - a` for descending, but a `reverse` parameter would be more ergonomic. Not part of this stage.
- **Mixed-type comparison errors** — sorting `[1, "hello"]` with default comparator errors. The alternative (coerce to string) is JS-style but adds complexity.
- **The comparator must return a number** — coercing to int would be forgiving but adds complexity. The discipline: **type-check the comparator's return value.**
- **The keyFn is called O(N log N) times in the worst case** (insertion sort is O(N²) comparisons, each comparison calls keyFn up to 2 times). For keyFn that's expensive, the caller can use a comparator instead. Not part of this stage.
- **No streaming sort** — the entire input is read into memory, the entire result is built in memory, then the result is returned. For very large arrays, this could be a memory concern, but for the "hand-rolled idiom" gap, the minimal API is the right default.

## My pick for Stage 44

After Stage 43, the "sort" pattern is established. The remaining "stdlib in lox" candidates are:
- `array_zip_longest(arr1, arr2, fill?)` — like Stage 37's array_zip but pads the shorter with a default.
- `is_array(value) -> bool` — separate type predicate. New native, not a fix to `typeof()`.
- `array_take(arr, n)` / `array_drop(arr, n)` — take/drop the first N elements.
- `array_take_while(arr, predicate)` / `array_drop_while(arr, predicate)` — take/drop while predicate is truthy.
- `array_intersect(arr1, arr2)` / `array_union(arr1, arr2)` / `array_difference(arr1, arr2)` — set operations.
- `array_count_by(arr, keyFn)` / `array_sum_by(arr, valueFn)` — counting and summing by key.
- **Modules** — ~600 lines, biggest swing. Tom's call, not mine.

**My pick for Stage 44: `is_array(value) -> bool`** — the natural next step. A small, focused native that closes a 27-stage-old gap (similar to Stage 39's typeof patch for OBJ_ARRAY). ~10 lines, no new architecture, no user-code dispatch. The new wrinkle: it's a "type predicate" native, not a "shape transform" or "higher-order" native. The trigger-mine bucket either grows beyond byox or we ship modules.

**After Stage 44, the next decision is one of:**
1. **`array_zip_longest`** — small extension of Stage 37's array_zip.
2. **`array_take` / `array_drop`** — slice operations.
3. **`array_intersect` / `array_union` / `array_difference`** — set operations.
4. **Modules (~600 lines, Tom's call)** — the biggest swing.
5. **Apply Stages 1-43 to a different project** — the trigger-mine bucket grows beyond byox.

The next-pick for Stage 44 will be decided at the moment of decision, per the freeze-pattern antidote.

---

**File paths** (all confirmed):
- `05-vm/clox/src/native.c` — added `sortCompareValues` helper (60 lines) + `arraySortNative` (80 lines) + 12 lines registration after `arrayGroupByNative`
- `05-vm/clox/tests/test_stdlib.c` — added 12 test functions + 12 main() invocations (293 lines)
- `05-vm/clox/examples/array-sort.lox` — new example, 206 lines
- `05-vm/clox/docs/stage-43-closeout.md` — this file

**Branch:** `stage-43-array-sort`, commit `70ca356` (impl + tests + example) + this file's commit.
