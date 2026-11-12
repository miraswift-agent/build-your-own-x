# Stage 61 close-out: array_slice(arr, start, end?) -> array

**Branch:** `stage-61-array-slice`
**Date:** 2026-07-19
**Tests:** 489/489 stdlib (was 478 at Stage 60; +11 assert-level checks across 8 new test functions)
**vm+clox total:** 529 passed, 0 failed (27 clox + 5 repl + 489 stdlib + 8 composability)
**Box total:** ~669 (92 allocator + 23 db + 25 shell + 529 vm+clox)
**Valgrind:** clean (exit 0 on all 4 test binaries, 0 errors, 0 leaks; stdlib 3024/3024 allocs/frees)
**Bugs caught:** 0 implementation bugs, 0 test bugs, 0 example bugs. **18th-consecutive-zero-implementation-bug stage in the new streak** (Stage 44 = 1st, Stage 61 = 18th). **NEW RECORD** — breaks the 17-streak from Stage 60 / prior Stages 27–43.

## What shipped

New native `array_slice(arr, start, end?) -> array`. Half-open range `[start, end)` with JS-style negative indices and clamping. Optional `end` defaults to `len`. Non-mutating; returns a new array.

Signature: `array_slice(arr, start, end?) -> array`

- `arr` must be an array.
- `start` must be a number; `end` if provided must be a number.
- Negative indices count from the end, then clamp to `[0, len]`.
- If `end < start` after clamp, result is empty.
- Neither input is mutated.

## The new wrinkle: half-open range + optional end + JS negatives

Not just `array_take(array_drop(arr, start), end-start)`:
1. optional end
2. negative indices
3. one native / one allocation
4. end is an absolute index, not a length (easy to mis-compose)

First 1-D range native; distinct from take/drop "first N / skip N".

## Architecture: zero new work

- Reuses Stage 12 (heap `ObjArray` + push/pop GC).
- Reuses Stage 50 (optional positional arg shape).
- Reuses Stage 51/52 patterns (size/index clamping mindset).
- Stage 44 push/pop lesson: 1 push result, 1 pop; registration 1 push name + 1 pop.

## Files changed

- `src/native.c`: `arraySliceNative` + registration as `array_slice`.
- `tests/test_stdlib.c`: 8 new test functions + `main()` calls.
- `examples/array-slice.lox` (new).
- `docs/stage-61-closeout.md` (this file).

## Tests (8 functions)

- basic middle slice
- omit end
- negative indices
- clamp and empty
- does not mutate source
- full copy via `array_slice(arr, 0)`
- wrong arg count
- wrong types

## Example verification

```
=== array_slice basic middle ===
3
20
30
40
=== array_slice omit end ===
2
40
50
=== array_slice negative indices ===
2
30
40
=== array_slice full copy ===
5
10
50
=== source unchanged ===
5
10
```

## Streak

18 consecutive zero-impl-bug stages (Stages 45–61 after Stage 44 broke the prior streak at registration). All-time high.

## Next candidates (Stage 62)

1. `array_partition(arr, predicate) -> array` — split `[pass, fail]`; HO sibling of filter. **Default pick** if slice family stays parked.
2. Modules — Tom's call.
3. browser-harden remainder / heartbeat mechanical tier (trigger-mine growth).
