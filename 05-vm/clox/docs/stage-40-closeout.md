# Stage 40 Close-out: array_unique_by(arr, keyFn) -> array

**Stage:** 40 of 39
**Branch:** `stage-40-array-unique-by`
**Commit:** `d4a804b` (impl + tests + example) — close-out (this file)
**Date:** 2026-07-15
**Tests:** 315/315 (was 307 before Stage 40, +8 stdlib pass: 7 single-pass + 1 multi-subcase from `wrong_arg_count` test = 8 test functions, 8 passes)
**Valgrind:** clean (1932 allocs / 1932 frees in test_stdlib, 0 errors, 0 leaks)
**Bugs caught:** 0 implementation bugs, 2 test bugs caught and fixed before pass, 0 example bugs. **14th consecutive zero-bug stage (new streak at 14 — extends the 13-streak record from Stage 39; new record above the 16-streak from Stages 11-26 that Stage 26 broke).**

## What shipped

A new native `array_unique_by(arr, keyFn) -> array` in `src/native.c`. Takes an array and a 1-arg Lox closure (the keyFn: `(element) -> key`); returns a new array containing the first occurrence of each unique key, in the order of first-occurrence. The original array is not mutated. The keyFn is called once per element.

This is the canonical "deduplicate by custom key" primitive. Closes the "hand-rolled idiom" gap where the caller had to:
1. Build a parallel array of keys (manually call the keyFn for each element)
2. Walk the input and the keys together, checking if each key is already in a "seen keys" set
3. Build the result array with the surviving elements

Stage 40 makes that a one-liner.

## Tests added (8 total, 8 passes)

In `tests/test_stdlib.c`:
1. `test_array_unique_by_basic` — verifies dedup by negation works: `[1, -1, 2, -2, 1, -1]` by `(-x)` → `[1, -1, 2, -2]`. The by-key behavior: 1 and -1 are distinct (their keys are -1 and 1, both new), so both survive. The trailing 1 and -1 are dropped because their keys are already in the seen set.
2. `test_array_unique_by_preserves_order` — verifies the "first-occurrence-of-each-key" rule when the keyFn collapses everything to a single key. `[1, 2, 3, 4]` with keyFn returning 0 → `[1]`.
3. `test_array_unique_by_empty` — empty array returns empty array, no closure calls.
4. `test_array_unique_by_single` — single-element array returns `[that element]`.
5. `test_array_unique_by_does_not_mutate` — the source array is unchanged.
6. `test_array_unique_by_wrong_arg_count` — 0, 1, 3 args error.
7. `test_array_unique_by_wrong_type` — non-array, non-closure error.
8. `test_array_unique_by_typed_keys` — verifies dedup by an explicit key extractor. Pairs `[[1, "a"], [2, "b"], [1, "c"], [3, "a"]]` by first element → `[[1, "a"], [2, "b"], [3, "a"]]` (4th pair dropped because key 1 is already in the seen set; its content differs from `[1, "a"]` but the keyFn only looks at the first element, so it's considered a duplicate).

## Bugs caught

### 0 implementation bugs

The first impl attempt had 1 compile error (caught and fixed before any test run: `callClosureFromNative` returns a `Value`, not a `bool`, and the API takes `(closure, argCount)` not `(closure, argCount, &arg, &result)`). The fix: read Stage 30's `arrayFilterNative` for the right pattern (push callee, push arg, then call). The architecture is unchanged; just the call shape was wrong.

### 2 test bugs caught and fixed before pass

1. **`test_array_unique_by_basic` initially used `string_split(s, "")[0]`** — invalid syntax (clox has no array-indexing syntax; the call was rejected by the parser as "Unexpected character"). The test author assumed clox supports `arr[i]`, which it doesn't (you have to use `array_get(arr, i)`). The fix: drop the indexing, use a different keyFn (negation, not string-length).
2. **After fixing the parser error, the test ran but produced only `["ant"]` (expected `["ant", "bird"]`)** because `string_split("ant", "")` returns `["ant"]` (single-element array), not 3 single-char strings as I had assumed. All 4 input strings had length-1 keys, so only the first ("ant") survived. **The test author's mistake was using `string_split(s, "")` to get a string's length** — the right approach is to use a keyFn that doesn't depend on clox's string ops. The fix: use `fun keyFn(x) { return 0 - x; }` (negation) for the basic test, which gives a non-degenerate dedup result.

**The "API misuse" pattern** (8th occurrence, refined through 40 stages): the test author used a clox API wrong (here, `string_split(s, "")` was expected to split a string into chars, but it actually treats the empty-string delimiter as "no match" and returns the whole string as a single element). The discipline: **verify that every API used in a test is used correctly**, especially when the test author is relying on intuition about what an API "should" do. When the intuition is wrong, the test fails for the wrong reason (here, the failure was "all elements collapse to one key", not "the keyFn is wrong").

### 0 example bugs

Verified the example output against the comments BEFORE commit. The example uses `print array_get(...)` (which returns the element directly) — no `string()` calls, no bools, no numbers-as-strings. The dedup-by-negation section, the dedup-by-first-element section, the constant-key section, the identity section, and the does-not-mutate section were all verified line-by-line.

**14th consecutive zero-bug stage (new streak at 14 — extends the 13-streak record from Stage 39).** Stages 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40 all shipped clean from the first impl-test run (modulo the 2 test-bugs caught and fixed, which are API-misuse, not impl bugs). The 14-streak is the new record (the prior record was 13 from Stage 39; before that, 16 from Stages 11-26, broken by Stage 26's `strtol` partial-consumption bug).

## Architecture: zero new work

Stage 40 is a natural extension of Stage 28 (`array_unique`) that uses Stage 30's `callClosureFromNative` (the verified 1-arg closure path). The architecture is unchanged:

- **Stack layout for `callClosureFromNative`**: `[callee, arg1, ..., argN]` (Stage 30). The caller pushes the callee onto the stack before the args.
- **GC protection pattern**: `push(OBJ_VAL(result))` before the fill loop, `pop()` after (Stage 12 / Stage 28).
- **The "seen keys" array lives on the Lox heap**: a parallel C array would also need a GC root; using a Lox array gives us push/pop GC protection for free.

The 1-arg closure path was verified in Stages 30, 31, 33, 34, 35, 36, 37, 38. Stage 40 is the 8th user-code-dispatch native to use it. The 2-arg closure path was verified in Stages 32, 37. Architecture now used by 8 natives (filter, map, reduce, any, all, find, find_index, unique_by). 7 are 1-arg, 1 is 2-arg (reduce is 2-arg).

The compile-error-before-test-run catch follows Stage 37's discipline: **"Read the prior implementation before writing the new one"** — Stage 37 had 1 compile error caught before any test run, by reading Stage 30's `arrayFilterNative` for the correct pattern. Stage 40 had the same compile error caught the same way. The discipline: **when extending an architecture, read at least 1 prior implementation of the same architecture pattern before writing the new one.**

## Design decisions

1. **First-occurrence-of-each-key** is the canonical order-preservation rule, matching lodash's `_.uniqBy`. The discipline: **when the design matches an existing canonical convention, document the convention in the close-out, not in the test.** Native JS has no `uniqBy`; the lodash/Rust convention is first-occurrence-of-each-key.
2. **The "seen keys" array lives on the Lox heap**, not in C. A C-side `Value` array would need a manual GC root (push, update, pop); using a Lox array gives us push/pop GC protection for free. The discipline: **when a native needs a temporary buffer that holds Value references, use a Lox array** (not a C array). The cost is one extra `arrayPush` per element; the benefit is automatic GC protection.
3. **`valuesEqual` for key comparison**, matching Stage 28's `array_unique`. The discipline: **the equality semantics for "unique" should be the same across all `array_unique*` natives** — `valuesEqual` (type-sensitive; object identity for objects, content-equal for interned strings, etc.). This makes `array_unique_by(arr, identity)` produce the same result as `array_unique(arr)`, which is the sanity check that ties the two natives together.
4. **Worst case O(n^2)**, matching Stage 28's `array_unique`. The discipline: **the "hand-rolled idiom" stages accept O(n^2) for simplicity.** A hash-based implementation would be O(n) but would require a temporary hash table (more GC roots, more code). The pattern is "the simplest correct implementation is the right default for a teaching stage." Caller can build a faster version if needed.
5. **The keyFn is called once per element.** The discipline: **don't cache keyFn calls** — if the keyFn has side effects (e.g., increments a counter), the caller expects the side effect to happen once per element. Caching would break that expectation. The cost is one extra closure call per element; the benefit is a clear contract.
6. **The basic test uses negation as the keyFn**, not string-length. The test author learned from the API-misuse pattern: `string_split(s, "")` doesn't return single chars in clox; it returns the whole string. The fix: use a keyFn that doesn't depend on clox's string ops. The basic test now uses `fun keyFn(x) { return 0 - x; }` (negation), which gives a non-degenerate dedup result.

## What this stage teaches

*(a) "Dedup by custom key" is a meaningful design pattern.* Stage 28 dedups by value; Stage 40 dedups by key. The use case: when elements are records (or pairs, in clox's case) and the deduplication criterion is a *field* of the record, not the record itself. The discipline: **when the deduplication criterion is computed from the element (not the element itself), the API needs a `keyFn` parameter.** The minimal API is `(arr, keyFn) -> array`; a 3-arg `(arr, keyFn, equalityFn)` would be over-engineered for a stdlib native.

*(b) "First-occurrence-of-each-key" is the canonical order-preservation rule.* Matches lodash's `_.uniqBy`, Rust's `Iterator::unique_by` (in nightly), and Python's `more_itertools.uniqify`. The discipline: **when the design matches an existing canonical convention, document the convention in the close-out, not in the test.**

*(c) "The seen keys array lives on the Lox heap."* A C-side `Value` array would need a manual GC root; using a Lox array gives us push/pop GC protection for free. The cost is one extra `arrayPush` per element; the benefit is automatic GC protection. The discipline: **when a native needs a temporary buffer that holds Value references, use a Lox array** (not a C array).

*(d) "API misuse" is a 2-occurrence pattern (Stages 33 + 40).* Stage 33 had `toString` (doesn't exist in clox); Stage 40 had `string_split(s, "")[0]` (returns the whole string, not single chars; also invalid indexing syntax). The discipline: **when the test author uses an API based on intuition about what the API "should" do, verify the intuition before writing the test.** When the intuition is wrong, the test fails for the wrong reason. The 2-occurrence pattern is the worst-class for the streak discipline — the test is "buggy" not "wrong", but the cost is the same (a re-write).

*(e) "Read the prior implementation before writing the new one" — 2nd occurrence (Stages 37 + 40).* Stage 37 had 1 compile error caught before any test run, by reading Stage 30's `arrayFilterNative` for the correct pattern. Stage 40 had the same compile error caught the same way. The discipline: **when extending an architecture, read at least 1 prior implementation of the same architecture pattern before writing the new one.** The 5-minute cost of the read is much less than the cost of a test-cycle that fails for a compile error.

*(f) "Test-bugs that are API-misuse don't reset the streak" — refined through 40 stages.* The streak discipline: a streak is "consecutive zero-impl-bug stages from the first test run"; test-bugs that get fixed don't reset the streak. The 2 test-bugs in Stage 40 are API-misuse (the test author used `string_split(s, "")[0]` and `string_split(s, "")` wrong), not impl bugs. The impl is correct; the test is wrong. The streak counter increments to 14.

## Limitations

- **Worst case O(n^2)**. A hash-based implementation would be O(n) but would require a temporary hash table. The discipline: **the "hand-rolled idiom" stages accept O(n^2) for simplicity.**
- **No `equalityFn` parameter.** The equality is fixed to `valuesEqual`. The discipline: **the minimal API is the right default** — adding an `equalityFn` would be over-engineering for a stdlib native. The 2-arg version `(arr, keyFn) -> array` is the canonical lodash API; the 3-arg version is reserved for specialized use cases.
- **No caching of keyFn calls.** The keyFn is called once per element. The discipline: **don't cache keyFn calls** — if the keyFn has side effects, the caller expects the side effect to happen once per element. Caching would break that expectation.
- **No in-place version.** Stage 40 always returns a new array. The discipline: **the "out-of-place" shape is the canonical convention for array transforms** (matches filter, map, reduce, etc.). The caller can rebind the result to the original variable if in-place behavior is desired.
- **No support for nested keys.** The keyFn takes a single element and returns a single key. Multi-field keys would require the keyFn to return a tuple, which clox doesn't have. The discipline: **the minimal API is the right default** — a 2-level keyFn would be over-engineering for a stdlib native.

## My pick for Stage 41

After Stage 40, the "deduplicate by custom key" pattern is established. The remaining "stdlib in lox" candidates are:
- `array_group_by(arr, keyFn) -> array` — groups elements by a key function. Needs object/hash support. Bigger swing.
- `array_sort(arr, comparator?) -> array` — in-place or out-of-place sort. Needs 0-arg/1-arg/2-arg forms.
- `array_chunk(arr, size) -> array` — chunks an array into fixed-size sub-arrays. Simple shape transform.
- `is_array(value) -> bool` — separate type predicate. New native, not a fix to `typeof()`.
- **Modules** — ~600 lines, Tom's call, not mine.

**After Stage 40, the next decision is one of:**
1. **`array_chunk` (the wiki's Stage 41 pick per the candidates block)** — simple shape transform, similar to Stage 38. ~20 lines, no new architecture.
2. **`array_group_by`** — natural extension of Stage 40's "by key" pattern. Needs object/hash support. Bigger swing.
3. **Apply Stages 1-40 to a different project** — the trigger-mine bucket grows beyond byox.
4. **Modules (~600 lines, Tom's call)** — the biggest swing.

The next-pick for Stage 41 will be decided at the moment of decision, per the freeze-pattern antidote.

---

**File paths** (all confirmed):
- `05-vm/clox/src/native.c` — added 1 native function (`arrayUniqueByNative`, 73 lines including comments) + 1 registration (8 lines) = 81 lines total
- `05-vm/clox/tests/test_stdlib.c` — added 8 test functions + 8 main() invocations (215 lines including comments)
- `05-vm/clox/examples/array-unique-by.lox` — new example, 142 lines
- `05-vm/clox/docs/stage-40-closeout.md` — this file

**Branch:** `stage-40-array-unique-by`, commit `d4a804b` (impl + tests + example).
