# Stage 60 close-out: array_difference(arr1, arr2, keyFn?) -> array

**Branch:** `stage-60-array-difference-by-key`
**Date:** 2026-07-19
**Tests:** 478/478 stdlib (was 473 at Stage 59, +5: 5 new Stage 60 by-key test functions; existing Stage 57 suite still green under the extended arity)
**vm+clox total:** 518 passed, 0 failed (27 clox + 5 repl + 478 stdlib + 8 composability)
**Box total:** ~658 (92 allocator + 23 db + 25 shell + 518 vm+clox)
**Valgrind:** clean (exit 0 on all 4 test binaries + example, 0 errors, 0 leaks)
**Bugs caught:** 0 implementation bugs, 0 test bugs, 0 example bugs. **17th-consecutive-zero-implementation-bug stage in the new streak** (Stage 44 = 1st, Stage 60 = 17th). **NEW RECORD** — breaks the 16-streak tie from Stage 59 and matches the prior 17-streak record (Stages 27–43).

## What shipped

Extended `array_difference` from Stage 57 to accept an **optional third argument**: a 1-arg key function. Registered under the same name `array_difference`, so existing 2-arg call sites continue to work unchanged (direct `valuesEqual` comparison / Stage 57 multiset subtraction), while 3-arg call sites get "difference by key."

Signature: `array_difference(arr1, arr2, keyFn?) -> array`

- `arr1`, `arr2` must be arrays.
- `keyFn`, if provided, must be a 1-arg closure.
- Multiset-by-key semantics: for each key k, result count = `max(0, count_in_arr1 with keyFn=k - count_in_arr2 with keyFn=k)`.
- Order: arr1 order of the kept (unmatched) elements.
- No drain of arr2 — difference is one-directional.
- Neither input is mutated.

## The new wrinkle: inverted fill + key-cache lockstep

Stage 58 established optional-closure + `remaining_keys` on the *intersect* walk (keep matches). Stage 59 reused both on the *union* walk/**drain**. Stage 60 reuses both on the *difference* walk (drop matches) — the inverted-fill direction from Stage 57, now with keys.

No drain means Stage 60 does not re-hit the Stage 59 drain-coordination risk, but the key cache must still stay in lockstep with `remaining` on every match shift during the walk. Get that wrong and later arr1 elements compare against stale keys and either over-drop or under-drop.

Architecture cost: ~zero new. Completes the parameterized set-ops triad (intersect/union/difference by key).

## Architecture: zero new work

- **Reuses Stage 12** (heap-allocated `ObjArray`s with push/pop GC protection).
- **Reuses Stage 30** (`callClosureFromNative` for 1-arg closure dispatch).
- **Reuses Stage 40** (`valuesEqual()` comparison on computed keys).
- **Reuses Stage 50** (optional positional arg shape).
- **Reuses Stage 57** (inverted-fill multiset difference walk).
- **Reuses Stage 58** (optional-closure + `remaining_keys` cache; O(|arr1|+|arr2|) keyFn calls).
- **Reuses Stage 59** (key-cache lockstep on shift).

Stage 44 push/pop lesson applied: function body pushes `remaining`, optionally `remaining_keys`, and `result`; pops in reverse before return. Registration: 1 push name + 1 pop.

## Files changed

- `src/native.c`:
  - Replaced Stage 57's `arrayDifferenceNative` with `arrayDifferenceByNative` (~120 lines including comments). 2-arg path is a backward-compatible shim over Stage 57; 3-arg path adds keyFn + remaining_keys.
  - Updated registration in `defineNatives()` to point `array_difference` at `arrayDifferenceByNative`.
- `tests/test_stdlib.c`:
  - Updated `test_array_difference_wrong_arg_count` so 2 or 3 args are valid; 1 or 4 error.
  - 5 new test functions: `test_array_difference_by_key_basic`, `test_array_difference_by_key_multiset`, `test_array_difference_by_key_objects`, `test_array_difference_by_key_omitted`, `test_array_difference_by_key_wrong_type`.
  - Added corresponding calls in `main()`.
- `examples/array-difference-by.lox` (new): 3-section example (objects by id, numeric key-class with basic + multiset cases, 2-arg backward compatibility).
- `docs/stage-60-closeout.md` (this file).

## Tests

- **`test_array_difference_by_key_basic`**: `[1,2,3,4,5] − [7,8]` by `x > 2` → `[1,2,5]` (drop two trues; keep F F + leftover true).
- **`test_array_difference_by_key_multiset`**: `[3,4,5] − [7]` by `x > 2` → `[4,5]` (one true consumed).
- **`test_array_difference_by_key_objects`**: Items by `.id`; arr1=`[a,b,d]` (ids 1,2,3), arr2=`[c]` (id 1) → `[b,d]`.
- **`test_array_difference_by_key_omitted`**: 2-arg form still produces Stage 57 output: `array_difference([1,1,2], [1]) = [1,2]`.
- **`test_array_difference_by_key_wrong_type`**: 3rd arg that is not a closure (e.g. `99`) is a runtime error.

## Example verification

```
=== array_difference by key: objects ===
kept.len:2
b
d
=== array_difference by key: numbers ===
basic.len:3
1
2
5
multi.len:2
4
5
=== array_difference 2-arg form still works ===
classic.len:2
1
2
```

## Next decision

Parameterized set-ops triad is COMPLETE (intersect-by-key + union-by-key + difference-by-key). Candidates for Stage 61:
- `array_slice(arr, start, end?)` — 3-arg 1-D slice; overlaps with take+drop composition.
- `array_partition(arr, predicate)` — split into [pass, fail] pair; natural HO sibling of filter.
- Modules — ~600 lines, Tom's call.
- Grow the trigger-mine bucket beyond byox (browser-harden remainder, heartbeat mechanical tier).

Tom has redirect rights at the next heartbeat.
