# Stage 57 close-out: array_difference(arr1, arr2) -> array

**Branch:** `stage-57-array-difference` (split turn — implementation + tests from the prior turn that errored mid-test (wrong stringification API, `to_string` doesn't exist; the fix was a 1-iteration rewrite to use `string(array_get(...))` like Stage 55/56's tests), then 1 test-bug caught pre-pass on the strings-test order expectation; this turn resumed: valgrind, example, close-out, commit, push, wiki catch-up)
**Date:** 2026-07-15 22:23 CDT (in-flight 21:23, resumed 22:23)
**Tests:** 464/464 stdlib (was 449 at Stage 56, +15: 11 test functions; 7 single-pass + 1 multi-subcase (3) + 3 single-pass subcases that are 1 each + 1 multi-subcase wrong-arg-count (2) + 1 multi-subcase wrong-type (2) = 11 test functions, 15 passes total)
**Valgrind:** clean (exit 0 on all 4 test binaries + the example, 0 errors, 0 leaks)
**Bugs caught:** 0 implementation bugs, 1 test bug caught and fixed pre-pass (strings test had wrong order expectation — expected `a\nb\n` but the impl correctly outputs `b\na\n` because the first `a` is dropped; the test was an order-mental-model miss, not an impl bug), 1 example bug caught pre-pass (the tag-diff example's expected output was wrong: it claimed 4 elements but the multiset-correct answer is 3, because arr2's `python` is consumed by the first arr1 `python` and the second arr1 `python` is kept; the result is `[vm, python, compiler]`, not `[python, vm, python, compiler]`). **14th-consec-zero-bug stage in the new streak** (Stage 44 = 1st, Stage 57 = 14th).

## What shipped

A new native `array_difference(arr1, arr2) -> array` in `src/native.c`. 2 arguments (both arrays). Returns a new array containing the **multiset difference** of arr1 and arr2. For each value v, the result has v count = `max(0, count_in_arr1 - count_in_arr2)`. Order of first appearance in arr1, with matched elements removed (an arr1 element that matches an arr2 element is dropped; the next non-matched arr1 element is appended, and so on). Neither input is mutated.

**The set operations family is now COMPLETE** (array_intersect, array_union, array_difference — the algebraic triad is 3 of 3). The next family decision is `array_intersect(arr1, arr2, keyFn?)` (extend Stage 55 with a keyFn for "intersect by key") or `array_slice` (3-arg variant with start, end) or `string_pad_end` (like Stage 22's string_pad_start but pads at the end) or modules (~600 lines, Tom's call).

## Architecture: zero new work

- **Reuses Stage 12** (heap-allocated ObjArrays with push/pop GC protection — `remaining` and `result` are both push/pop'd, balanced 2-in / 2-out after the loop).
- **Reuses Stage 30** (push/pop discipline, the Stage 44 lesson applied for the 14th consecutive stage).
- **Reuses Stage 40** (`valuesEqual()` comparison — same as Stage 55/56's set operations).
- **Reuses Stage 55/56** (the "remaining copy" pattern — but the **fill direction is INVERTED** from both prior set ops; see "the new wrinkle" below).

The 2-array-iteration pattern is now well-established:
- Stage 37 (zip): read-only flavor, both inputs.
- Stage 50 (zip_longest): read-only flavor, both inputs, with optional fill.
- Stage 55 (intersect): mutate-working-copy flavor, walks arr1, decrements `remaining`, **skips unmatched**.
- Stage 56 (union): mutate-working-copy flavor, walks arr1, decrements `remaining`, **appends every arr1 element**, then drains `remaining`.
- Stage 57 (difference): **mutate-working-copy flavor, walks arr1, decrements `remaining`, drops matched arr1 elements, keeps only unmatched.**

The shapes are similar; the fill direction is different. The discipline: **for set operations, the walking-strategy depends on whether the op is intersection (skip), union (append+then-drain), or difference (drop-if-matched).**

## The new wrinkle: multiset vs unique — and the FILL DIRECTION is INVERTED

The wiki's pick for Stage 57 named multiset subtraction: count of v in result = `max(0, c1 - c2)`. There are **two reasonable semantics** for array_difference:

1. **`max(0, c1 - c2)`** (Python `Counter - Counter`, mathematical multiset-subtraction) — **chosen**
2. **`1` if v in arr1 and not in arr2, else `0`** (Python `set - set`, unique-difference)

I went with (1) `max(0, c1 - c2)` because:
- It matches the **mathematical** definition of multiset subtraction (c_diff(v) = max(0, c1(v) - c2(v))).
- It's the **Python stdlib `Counter - Counter`** semantics.
- It makes `array_intersect`, `array_union`, and `array_difference` form a natural algebraic triad: `intersect` uses `min`, `union` uses `max`, `difference` uses `max(0, c1 - c2)`. The asymmetry is symmetric.
- The first arg is the "from" set, the second is "what to subtract" — matches Python's `Counter - Counter` convention and the language convention that the FIRST arg is the source.

If Tom prefers (2) unique-difference, the impl is a small change:
- (2) unique: deduplicate arr1 (or just check membership-in-arr2 as you walk), drop if v in arr2. ~10-line change. (Or compose: `array_unique(arr1) - arr2 = filter(arr1, fn(x) => !contains(arr2, x))` with the existing primitives.)

**This is a Tom-callable decision.** The morning-briefing will surface it. The impl, tests, and example are all consistent with the `max(0, c1 - c2)` choice.

### The FILL DIRECTION INVERSION — the structural wrinkle of Stage 57

Stages 55, 56, 57 all use the "remaining copy" pattern: build a copy of arr2, walk arr1, decrement `remaining` on matches. But the APPEND CONDITION is different for each:

- **Stage 55 (intersect):** append if matched. The `remaining` decrement happens AND then the element is appended. The element is appended once per match.
- **Stage 56 (union):** always append from arr1, then drain `remaining`. The `remaining` decrement happens AND then the element is appended. The result is the concatenation of arr1 (with decremented remaining) and the leftover remaining (drained).
- **Stage 57 (difference):** append only if NOT matched. The `remaining` decrement happens AND THEN the element is NOT appended (because matched). If the element doesn't match remaining, it's appended.

The three append conditions form a complete set: "match-and-append" (intersect), "match-and-append-and-drain" (union), "match-and-drop" (difference). After Stage 57, the pattern's three flavors are all on disk; future set-op variants (e.g., symmetric-difference: `union(a, b) - intersect(a, b)` = `a xor b` multiset) can be composed from the three primitives.

## One-directional subtraction

`array_difference` is **not symmetric**: `difference(a, b) != difference(b, a)` in general. Example: `difference([1, 2, 3], [4, 5, 6]) = [1, 2, 3]` (all kept), but `difference([4, 5, 6], [1, 2, 3]) = [4, 5, 6]` (all kept). The native only computes the FIRST form ("what's in arr1 that's not in arr2"). The caller computes the second by swapping args.

This matches Python's `Counter - Counter` and the convention that the FIRST arg is the "from" set.

## Files changed

- `src/native.c`:
  - `arrayDifferenceNative()` — ~95 lines including extensive comments. The Stage 44 push/pop lesson applied (2 pushes, 2 pops after the loop). The "remaining copy" pattern reused from Stage 55/56.
  - Registration in `defineNatives()` after `array_union`. 1 push (for the name), 1 pop (after tableSet). Balanced.
- `tests/test_stdlib.c`:
  - 11 new test functions covering: basic, empty inputs (3 sub-cases), no overlap, full overlap, multiset, arr1 dominates, arr2 dominates, strings, does-not-mutate, wrong arg count (2 sub-cases), wrong type (2 sub-cases). 15 total passes.
  - 11 new test function calls in `runTests()` between the Stage 56 tests and Stage 29's string_split tests.
- `examples/array-difference.lox` (new): 11-section example, 10686 bytes. Use cases include "tasks remaining" (subtract completed from all) and "tag diff" (subtract shared tags to see what's unique to article 1).
- `docs/stage-57-closeout.md` (this file).

## Tests

- **`test_array_difference_basic`**: `[1, 2, 3] - [2, 4] = [1, 3]`. The 2 matches and is dropped; 1 and 3 are not in arr2 and are kept.
- **`test_array_difference_empty_inputs`** (3 sub-cases): `[] - [1, 2, 3] = []`, `[1, 2, 3] - [] = [1, 2, 3]`, `[] - [] = []`.
- **`test_array_difference_no_overlap`**: `[1, 2, 3] - [4, 5, 6] = [1, 2, 3]`. Nothing matches, all kept.
- **`test_array_difference_full_overlap`**: `[1, 2, 3] - [1, 2, 3] = []`. Every arr1 element is matched and dropped.
- **`test_array_difference_multiset`** (the defining test): `[1, 1, 2] - [1] = [1, 2]`. The first 1 is matched and dropped; the second 1 is kept (the "extra" beyond arr2's count); 2 is not in arr2 and is kept.
- **`test_array_difference_arr1_dominates`**: `[1, 1, 1, 1] - [1, 2] = [1, 1, 1]`. The 1 matches once (dropped); the remaining 3 1's are kept; 2 has no arr1 copies so it's irrelevant.
- **`test_array_difference_arr2_dominates`**: `[1, 2] - [1, 1, 1, 1] = [2]`. The 1 matches and is dropped; 2 is kept; the 3 "extra" 1's in arr2 are ignored.
- **`test_array_difference_strings`**: `["a", "b", "a"] - ["a"] = ["b", "a"]`. Same multiset semantics. (The first "a" is matched and dropped; "b" is kept; the second "a" is kept. Order: first-appearance-in-arr1 with matched removed.)
- **`test_array_difference_does_not_mutate`**: Verifies a1 and a2 contents after the call. `a1 = [1, 2, 3]`, `a2 = [2, 4]`, `d = a1 - a2 = [1, 3]`. After: a1.len:3, a1.0:1, a1.1:2, a1.2:3, a2.len:2, a2.0:2, a2.1:4, d.len:2, d.0:1, d.1:3.
- **`test_array_difference_wrong_arg_count`** (2 sub-cases): 1 arg + 3 args both error.
- **`test_array_difference_wrong_type`** (2 sub-cases): non-array 1st arg + non-array 2nd arg both error.

## Multiset correctness verification (by hand)

For each test, I traced the algorithm:

- `difference([1, 2, 3], [2, 4])`:
  - remaining = [2, 4] (copy of arr2)
  - i=0, v=1: not in remaining, append. result=[1].
  - i=1, v=2: in remaining, consume 2. remaining=[4]. Don't append. result=[1].
  - i=2, v=3: not in remaining, append. result=[1, 3].
  - Final: [1, 3]. ✓

- `difference([1, 1, 2], [1])`:
  - remaining = [1]
  - i=0, v=1: in remaining, consume. remaining=[]. Don't append. result=[].
  - i=1, v=1: not in remaining (empty), append. result=[1].
  - i=2, v=2: not in remaining, append. result=[1, 2].
  - Final: [1, 2]. ✓

- `difference([1, 2], [1, 1, 1, 1])`:
  - remaining = [1, 1, 1, 1]
  - i=0, v=1: in remaining at j=0, consume. remaining=[1, 1, 1]. Don't append. result=[].
  - i=1, v=2: not in remaining, append. result=[2].
  - Final: [2]. ✓

- `difference([1, 1, 1, 1], [1, 2])`:
  - remaining = [1, 2]
  - i=0, v=1: in remaining at j=0, consume. remaining=[2]. Don't append. result=[].
  - i=1, v=1: not in remaining (j=0 is now 2, no match), append. result=[1].
  - i=2, v=1: not in remaining, append. result=[1, 1].
  - i=3, v=1: not in remaining, append. result=[1, 1, 1].
  - Final: [1, 1, 1]. ✓

## The Stage 44 push/pop lesson — applied for the 14th consecutive stage

The "Stage 44 lesson" is: every native that allocates on the Lox heap must `push` the new object to the GC stack and `pop` it after the function returns or before leaving the active scope. The pattern in `arrayDifferenceNative`:

```c
ObjArray *remaining = newArray(arr2->count);
push(OBJ_VAL(remaining));  /* GC: keep alive while filling */
for (...) { arrayPush(remaining, ...); }
ObjArray *result = newArray(0);
push(OBJ_VAL(result));  /* GC: keep alive while filling */
for (...) { ... arrayPush(result, elem); }
pop();  /* pop the result array */
pop();  /* pop the remaining array */
return OBJ_VAL(result);
```

Two pushes inside the function body, two pops after the loops. Balanced.

The registration in `defineNatives()` is 1 push + 1 pop, also balanced. After Stage 57, the pattern is now 14 stages deep (Stages 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57 — all of Stages 44–57 have applied it).

## What would change the Stage 58 pick

- (a) Telegram from Tom redirecting byox scope.
- (b) Tom's review of the multiset-vs-unique choice for Stage 57 (if he prefers unique-difference, that flags a wider set-ops-direction question).
- (c) `git log --all | grep -i "stage 58"` showing prior work (checked: none).
- (d) A re-run of valgrind surfacing leaks in Stage 57 on a re-run (low risk — fresh, ran clean at 464/464 + example).
- (e) The wiki INDEX being stale again (just caught a 2-stale miss + a duplicate-row bug in the Stage 56 turn; the wiki is at 56 with one row each; this turn's catch-up will land it at 57).
- (f) Tom signaling Stage 58 should extend Stage 55 with `keyFn?` instead of going to slice / string / modules.
- (g) `array_slice` (3-arg) being more pressing than extending the set-op family.
- (h) The 14-streak breaking — Stage 58 is the 15th-consec-zero-bug. **1 short of tying the 16-streak record; 2 short of the 17-streak record. Stage 58 is the 15th-streak-tying-attempt.** If Stage 58 lands clean, the 15-streak is set (a new sub-record: 15 consecutive zero-bug stages in the new streak, tying the post-Stage-44 streak). If it also lands clean, the 16-streak is TIED with the 16-streak record from Stages 11-26 (Stage 26 broke that). Stage 59 is the 16-streak-tying-attempt.

## Stage 58 candidates (post-Stage 57)

1. **`array_intersect(arr1, arr2, keyFn?)`** — extend Stage 55 with a keyFn for "intersect by key" (mirror of Stage 40's array_unique_by). ~30 lines, reuses Stage 40's callClosureFromNative. Optional positional param like Stage 50. The natural next pick now that the simple set ops are done.
2. **`array_slice(arr, start, end?)`** — 3-arg 1-D slice. ~30 lines. The "sub-array" idiom. Overlaps with `array_take` + `array_drop` composition.
3. **`string_pad_end(s, n, char?)`** — like Stage 22's string_pad_start but pads at the end. ~25 lines, low risk.
4. **Modules** — ~600 lines, biggest swing, Tom's call.

My pick for Stage 58 is **#1 `array_intersect(arr1, arr2, keyFn?)`** because:
- It builds on the just-completed set-ops family (3 of 3 done, now extending Stage 55).
- It's a Tom-callable extension that's small (~30 lines) but tests the optional-arg + closure-arg combination (already used by Stage 43's array_sort and Stage 50's array_zip_longest).
- It's a high-value gap (the "intersect by key" pattern is the most-requested extension of array_intersect in the Python/Ruby/JS stdlibs).
- After Stage 58, the next decision is `array_slice` or `string_pad_end` (close-the-gap primitives) or modules (Tom's call) or grow the trigger-mine bucket beyond byox.

## Open questions (carried forward)

- (AGENTS.md containment addition) — added in commit `9b4be23`, the rule is now self-enforcing.
- (Stale calendar OAuth scope) — still pending.
- (Engram P0 silent-pipeline fix) — still pending Tom's call.
- (Top-level build-your-own-x README still 18+ stages stale).
- (.pi/agent working tree has 12+ files modified by other pi children in the orchestrator; not committed per "commit only what I touched" discipline).

## Test count progression (post-Stage 57)

- Allocator 92 (unchanged)
- Database 23 (unchanged)
- Shell 25 (unchanged)
- VM — clox 504 (was 489, +15: 11 test functions, 15 passes)
- **Total on box: 644** (was 629 at Stage 56; +15 stdlib pass)

**Zero-bug streak: 14** (Stage 44 = 1, Stage 57 = 14). 2 short of the 16-streak record, 3 short of the 17-streak record. The next 2 stages (58-59) are the closing-in period.
