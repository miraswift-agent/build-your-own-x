# Stage 56 close-out: array_union(arr1, arr2) -> array

**Branch:** `stage-56-array-union` (split turn — implementation + tests from the prior turn that errored at the valgrind bash call; this turn resumed: valgrind, example, close-out, commit, push, wiki catch-up)
**Date:** 2026-07-15 21:23 CDT (in-flight 20:53, resumed 21:23)
**Tests:** 449/449 stdlib (was 435 at Stage 55, +14: 10 single-pass + 1 multi-subcase wrong-arg-count (2) + 1 multi-subcase wrong-type (2) = 10 test functions, 14 passes total)
**Valgrind:** clean (exit 0 on all 4 test binaries + the example, 0 errors, 0 leaks)
**Bugs caught:** 0 implementation bugs, 0 test bugs, 0 example bugs (11 sections, all match comments on the first run). **13th-consec-zero-bug stage in the new streak** (Stage 44 = 1st, Stage 56 = 13th).

## What shipped

A new native `array_union(arr1, arr2) -> array` in `src/native.c`. 2 arguments (both arrays). Returns a new array containing the **multiset union** of arr1 and arr2. For each value v, the result has v count = `max(count_in_arr1, count_in_arr2)`. Order of first appearance in arr1 is preserved; then first appearance in arr2 of values not yet in the result, in their arr2 order. Neither input is mutated.

**The set operations family is now 2 deep** (array_intersect, array_union). Stage 57 = `array_difference` is the natural next pick (multiset subtraction: count = max(0, c1 - c2)).

## Architecture: zero new work

- **Reuses Stage 12** (heap-allocated ObjArrays with push/pop GC protection — `remaining` and `result` are both push/pop'd, balanced 2-in / 2-out after the loop).
- **Reuses Stage 30** (push/pop discipline, the Stage 44 lesson applied for the 13th consecutive stage).
- **Reuses Stage 40** (`valuesEqual()` comparison — same as Stage 55's array_intersect and Stage 28/40's array_unique/array_unique_by).
- **Reuses Stage 55** (the "remaining copy" pattern — but the fill direction is different; see "the new wrinkle" below).

The 2-array-iteration pattern is now well-established:
- Stage 37 (zip): read-only flavor, both inputs.
- Stage 50 (zip_longest): read-only flavor, both inputs, with optional fill.
- Stage 55 (intersect): mutate-working-copy flavor, walks arr1, decrements `remaining`, skips unmatched.
- Stage 56 (union): **mutate-working-copy flavor, walks arr1, decrements `remaining`, AND appends every arr1 element, then drains `remaining`.**

The shapes are similar; the fill direction is different. The discipline: **for set operations, the walking-strategy depends on whether the op is intersection (skip) or union/difference (append).**

## The new wrinkle: multiset vs unique vs sum

The wiki's pick for Stage 56 contained an ambiguous spec — "count = count1 + count2" in the leading spec, but "deduplicated by min-count" in the parenthetical. There are **three reasonable semantics** for array_union:

1. **`max(c1, c2)`** (Python `Counter | Counter`, mathematical multiset-union) — **chosen**
2. **`c1 + c2`** (Python `Counter + Counter`, multiset-sum)
3. **`1` if v in either, `0` otherwise** (Python `set | set`, unique-union)

I went with (1) `max(c1, c2)` because:
- It matches the **mathematical** definition of multiset union (c_union(v) = max(c1(v), c2(v))).
- It's the **Python stdlib `Counter | Counter`** semantics — the most-familiar reference for a Lox developer.
- It makes `array_intersect` and `array_union` form a natural pair: `intersect` uses `min`, `union` uses `max`. The asymmetry is symmetric.
- It composes: `array_union(arr, []) == arr` (no extras), `array_union([1,1,1], [1]) == [1,1,1]` (max(3,1)=3).

If Tom prefers (2) or (3), the impl is a small change:
- (2) sum: remove the decrement in the first walk, then append the full arr2 instead of just `remaining`. ~3-line change.
- (3) unique: drop the "remaining copy" pattern entirely; use a "seen" set with a 1-arg closure or a one-pass scan with a third result array that dedups by append-only-if-not-yet-seen. ~10-line change.

**This is a Tom-callable decision.** The morning-briefing will surface it. The impl, tests, and example are all consistent with the `max(c1, c2)` choice.

## Files changed

```
05-vm/clox/src/native.c          | +142  (Stage 56: arrayUnionNative + registration in defineNatives)
05-vm/clox/tests/test_stdlib.c   | +265  (10 new test functions, registered in runTests)
05-vm/clox/examples/array-union.lox | new (8715 bytes, 11 sections)
05-vm/clox/docs/stage-56-closeout.md | new (this file)
```

The implementation is ~80 lines of native + extensive comments documenting the semantics choice and the alternatives.

## Tests added (10 functions, 14 sub-cases)

1. `test_array_union_basic` — `[1,2,3] ∪ [3,4,5]` → `[1,2,3,4,5]`
2. `test_array_union_empty_inputs` — `[] ∪ [1,2,3]`, `[1,2,3] ∪ []`, `[] ∪ []` all correct (3 sub-cases)
3. `test_array_union_no_overlap` — `[1,2] ∪ [3,4]` → `[1,2,3,4]`
4. `test_array_union_full_overlap` — `[1,2,3] ∪ [1,2,3]` → `[1,2,3]`
5. `test_array_union_multiset` — `[1,1,2] ∪ [1,2,2]` → `[1,1,2,2]` (the defining test for max(c1, c2))
6. `test_array_union_arr1_dominates` — `[1,1,1,1] ∪ [1,2]` → `[1,1,1,1,2]`
7. `test_array_union_arr2_dominates` — `[1,2] ∪ [1,1,1,1]` → `[1,2,1,1,1]`
8. `test_array_union_does_not_mutate` — verifies a1.len:3, a2.len:3, r.len:4
9. `test_array_union_wrong_arg_count` — 1 arg + 3 args both error (2 sub-cases)
10. `test_array_union_wrong_type` — non-array 1st arg + non-array 2nd arg both error (2 sub-cases)

All 10 functions passed on the first run. 0 test bugs.

## Example (11 sections, all match comments on first run)

The example `examples/array-union.lox` walks through:
- Basic case (no overlap)
- The defining multiset test (`[1,1,2] ∪ [1,2,2] = [1,1,2,2]`)
- arr1 dominates
- arr2 dominates
- Full overlap
- No overlap (pure concatenation)
- Empty inputs (3 sub-cases)
- Does-not-mutate
- Strings (value equality)
- A real use case (combined tag set for two articles)
- "What this stage teaches" — 6 lessons including the multiset-vs-unique-vs-sum decision and the 13-streak

**0 example bugs.** The example runs cleanly under valgrind (exit 0, 0 errors, 0 leaks).

## Live test counts (post-Stage 56)

- allocator: 92
- db: 23
- shell: 25
- vm+clox: 489 (= 27 + 5 + 8 + 449)
- **total: 629** (was 615 at Stage 55, +14)
- **zero-bug streak: 13** (Stage 44 = 1, Stage 56 = 13)

3 short of the 16-streak record, 4 short of the 17-streak record. The next 3 stages (57-59) are the closing-in period; if all 3 land clean, the 16-streak is tied at Stage 59, and Stage 60 is the tie-breaker.

## What would change the Stage 57 pick

1. **Telegram from Tom redirecting byox scope** — same as every prior stage; the wiki's candidate list is my pick, not Tom's directive.
2. **Tom's review of the multiset-vs-unique-vs-sum choice for Stage 56** — if he prefers `count1 + count2` or unique union, that's a 3-10 line change in Stage 56 and a flag for the Stage 57 design. This is the most likely thing to change the Stage 57 pick.
3. **`git log --all | grep -i "stage 57"`** showing prior work — checked, none.
4. **A re-run of valgrind surfacing leaks in Stage 56** — checked, all clean.
5. **The wiki INDEX being stale** — caught the 19:35 turn's silent table-row miss earlier today; current now.
6. **`array_difference` being more pressing than other Stage 57 candidates** — the set operations family is the natural next pick; if Tom signals "go deeper on set ops", Stage 57 = `array_difference`. If he signals "switch families", Stage 57 = `array_slice` or `string_pad_end` or `array_intersect(arr1, arr2, keyFn?)`.
7. **The 13-streak breaking** — Stage 57 is the 14th-consec-zero-bug, and a single bug would reset it. The streak record is 17, set at Stage 43.
8. **Tom's call on a new project in the trigger-mine bucket** — the wiki's "5. Apply Stages 1-56 to a different project" is the only M-owned bucket-growth path; if Tom says "yes branch sideways", Stage 57 is on a new project, not byox.

## My pick for Stage 57

`array_difference(arr1, arr2) -> array` — third set operation. Multiset subtraction: count of v in result = `max(0, c1(v) - c2(v))`. ~30 lines, no user-code dispatch, the same "mutate a working copy" shape as Stage 55/56. After Stage 57, the set operations family is 3 deep and complete (intersect, union, difference — the algebraic triad).

The new wrinkle for Stage 57: subtraction has a different shape than intersection or union because values can appear in arr1 that are NOT in arr2 (so the result might be a "filtered copy of arr1" plus a "drained remaining"). The algorithm is:
- Walk arr1, push v to result. If v in `remaining`, decrement `remaining` (so the v was "subtracted"). If v not in `remaining`, the v stays in result with full count.
- The result is arr1 with each v's count = max(0, c1(v) - c2(v)).

This is well-defined, ~30 lines, and would be the third set op. After Stage 57 the set operations family is complete and the next candidate is `array_intersect(arr1, arr2, keyFn?)` (Stage 40's callClosureFromNative pattern, optional 3rd arg) or `array_slice` or `string_pad_end`.

## The split-turn discipline (meta-lesson)

This was a **split turn**. The prior turn (20:53 CDT) wrote the implementation, the tests, and started a valgrind bash call. The valgrind call errored at the redirect level (the prior turn's final tool result was "No result provided" — likely a tool call that errored mid-stream). The current turn resumed by:

1. Re-verifying the working tree (clean from prior turn's edits, on `stage-56-array-union` branch).
2. Re-running `make` (clean — no rebuild needed).
3. Re-running `make test` (449/449 stdlib pass).
4. Re-running valgrind on all 4 test binaries (all clean).
5. Writing the example file.
6. Running the example, verifying output (0 bugs).
7. Valgrinding the example (clean).
8. Writing this close-out.
9. Commit, push, wiki catch-up, daily log.

**The discipline: a split turn is a "I stopped, then I resumed" turn, not a "I failed, then I retried" turn. The data from the prior turn (449/449 stdlib, +14 stdlib) was correct; the bash call was the only thing that errored. The next turn picks up where the last one stopped, not where it broke.**

## Status of OTHER open-loop items (not touched this turn)

- AGENTS.md containment addition: still Tom's call. (All open-questions RESOLVED.)
- Stale calendar OAuth scope: still pending.
- Engram P0 silent-pipeline fix: still pending Tom's call.
- Top-level build-your-own-x README still 17+ stages stale.
- .pi/agent working tree has 12+ files modified by other pi children in the orchestrator; not committed per "commit only what I touched" discipline.

## No Telegram sent

Per the loop's discipline: the morning-briefing is the visibility layer, not the heartbeat. The multiset-vs-unique-vs-sum decision will be surfaced in the morning-briefing, not via Telegram.
