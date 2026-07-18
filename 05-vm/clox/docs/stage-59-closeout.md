# Stage 59 close-out: array_union(arr1, arr2, keyFn?) -> array

**Branch:** `stage-59-array-union-by-key`
**Date:** 2026-07-18
**Tests:** 473/473 stdlib (was 468 at Stage 58, +5: 5 new Stage 59 by-key test functions; existing Stage 56 suite still green under the extended arity)
**vm+clox total:** 513 passed, 0 failed (27 clox + 5 repl + 473 stdlib + 8 composability)
**Box total:** ~653 (92 allocator + 23 db + 25 shell + 513 vm+clox)
**Valgrind:** clean (exit 0 on all 4 test binaries, 0 errors, 0 leaks; stdlib 2928/2928 allocs/frees)
**Bugs caught:** 0 implementation bugs, 0 test bugs, 0 example bugs in this close-out pass. **16th-consecutive-zero-implementation-bug stage in the new streak** (Stage 44 = 1st, Stage 59 = 16th). Ties the 16-streak record from Stages 11–26 that Stage 26 broke; 1 short of the 17-streak record (Stages 27–43).

## What shipped

Extended `array_union` from Stage 56 to accept an **optional third argument**: a 1-arg key function. Registered under the same name `array_union`, so existing 2-arg call sites continue to work unchanged (direct `valuesEqual` comparison / Stage 56 multiset union), while 3-arg call sites get "union by key."

Signature: `array_union(arr1, arr2, keyFn?) -> array`

- `arr1`, `arr2` must be arrays.
- `keyFn`, if provided, must be a 1-arg closure.
- Multiset-by-key semantics: for each key k, result count = `max(count_in_arr1 with keyFn=k, count_in_arr2 with keyFn=k)`.
- Order: all of arr1 first (arr1 order), then leftover arr2 elements whose key counts exceed arr1's.
- Neither input is mutated.

## The new wrinkle: key-cache lockstep on the drain

Stage 58 already established optional-closure + `remaining_keys` cache on the *intersect* walk. Stage 59 reuses both on the *union* walk/**drain**. The drain is the Stage 56 wrinkle that Stage 58 never hit: after walking arr1 (append every element, consume one remaining match by key), leftover `remaining` elements are appended. The key cache must stay in lockstep with `remaining` as matches are shifted out — every shift of `remaining[j..]` also shifts `remaining_keys[j..]`. Get that wrong and the drain still produces a result, but the *next* match during the walk compares against a stale key.

This is the first stage that exercises the Stage 58 key-cache pattern on a native whose fill direction includes a drain. Architecture cost: ~zero new. The risk is coordination, not invention.

## Architecture: zero new work

- **Reuses Stage 12** (heap-allocated `ObjArray`s with push/pop GC protection).
- **Reuses Stage 30** (`callClosureFromNative` for 1-arg closure dispatch).
- **Reuses Stage 40** (`valuesEqual()` comparison on computed keys).
- **Reuses Stage 50** (optional positional arg shape).
- **Reuses Stage 56** (walk-then-drain multiset union).
- **Reuses Stage 58** (optional-closure + `remaining_keys` cache; O(|arr1|+|arr2|) keyFn calls).

Stage 44 push/pop lesson applied: function body pushes `remaining`, optionally `remaining_keys`, and `result`; pops in reverse before return. Registration: 1 push name + 1 pop.

## Files changed

- `src/native.c`:
  - Replaced Stage 56's `arrayUnionNative` with `arrayUnionByNative` (~130 lines including comments). 2-arg path is a backward-compatible shim over Stage 56; 3-arg path adds keyFn + remaining_keys.
  - Updated registration in `defineNatives()` to point `array_union` at `arrayUnionByNative`.
- `tests/test_stdlib.c`:
  - Updated `test_array_union_wrong_arg_count` so 2 or 3 args are valid; 1 or 4 error.
  - 5 new test functions: `test_array_union_by_key_basic`, `test_array_union_by_key_arr2_extras`, `test_array_union_by_key_objects`, `test_array_union_by_key_omitted`, `test_array_union_by_key_wrong_type`.
  - Added corresponding calls in `main()`.
- `examples/array-union-by.lox` (new): 3-section example (objects by id, numeric key-class with covered + extras cases, 2-arg backward compatibility).
- `docs/stage-59-closeout.md` (this file).

## Tests

- **`test_array_union_by_key_basic`**: `[1,2,3,4,5]` ∪ `[7,8,9]` by `x > 2` → `[1,2,3,4,5]` (arr1 already covers true-count 3; false only in arr1).
- **`test_array_union_by_key_arr2_extras`**: `[3]` ∪ `[7,8,9]` by `x > 2` → `[3,8,9]` (true-count max=3; 7 consumed as match for 3; 8,9 drain).
- **`test_array_union_by_key_objects`**: Items by `.id`; arr1=`[a,b]` (ids 1,2), arr2=`[c,d]` (ids 1,3) → `[a,b,d]` (c's id consumed by a).
- **`test_array_union_by_key_omitted`**: 2-arg form still produces Stage 56 output: `array_union([1,1,2], [1,2,2]) = [1,1,2,2]`.
- **`test_array_union_by_key_wrong_type`**: 3rd arg that is not a closure (e.g. `99`) is a runtime error.

## Example verification

```
=== array_union by key: objects ===
merged.len:3
a
b
d
=== array_union by key: numbers ===
covered.len:5
1
2
3
4
5
extras.len:3
3
8
9
=== array_union 2-arg form still works ===
classic.len:4
1
1
2
2
```

## Next decision

Parameterized set-ops pair for the two most-used ops is complete (intersect-by-key + union-by-key). Candidates for Stage 60:
- `array_difference(arr1, arr2, keyFn?)` — complete the parameterized triad (mirror of Stage 57). **My default pick.**
- `array_slice(arr, start, end?)` — 3-arg 1-D slice; overlaps with take+drop composition.
- Modules — ~600 lines, Tom's call.
- Grow the trigger-mine bucket beyond byox (loop v0.2).

Tom has redirect rights at the next heartbeat.
