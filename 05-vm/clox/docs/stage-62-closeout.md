# Stage 62 close-out: array_partition(arr, predicate) -> array

**Branch:** `stage-62-array-partition`
**Date:** 2026-07-19
**Tests:** 499/499 stdlib (was 489 at Stage 61; +10 pass() calls across 8 test functions)
**vm+clox total:** 539 passed, 0 failed (27 clox + 5 repl + 499 stdlib + 8 composability)
**Box total:** ~679 (92 allocator + 23 db + 25 shell + 539 vm+clox)
**Valgrind:** clean (exit 0 on all 4 test binaries, 0 errors, 0 leaks; stdlib 3084/3084 allocs/frees)
**Bugs caught:** 0 implementation bugs. 1 test-author issue fixed pre-pass (clox has no integer mod — used `n > 3` instead of evenness via `/`). 0 example bugs. **19th-consecutive-zero-implementation-bug stage** (Stage 44 = 1st, Stage 62 = 19th). **NEW RECORD**.

## What shipped

New native `array_partition(arr, predicate) -> array`. One pass over `arr`; returns a 2-element array `[pass, fail]` where `pass` holds elements for which the 1-arg predicate is truthy and `fail` holds the rest. Order preserved on both sides. Non-mutating.

Signature: `array_partition(arr, predicate) -> array`

- `arr` must be an array; `predicate` must be a 1-arg closure.
- Truthiness matches `array_filter` (only `false` and `nil` are falsy).
- Empty source → `[[], []]`.

## The new wrinkle: two result arrays under one outer array

GC-protect `pass`, `fail`, and `outer` while the predicate runs (3 pushes / 3 pops). Same HO dispatch as Stage 30; no new architecture beyond the nested-result shape.

Why not `filter` + inverse-filter: one pass, one dispatch cost per element, pair is a natural unit.

## Architecture

- Reuses Stage 12 (heap `ObjArray` + push/pop GC).
- Reuses Stage 30 (`callClosureFromNative` 1-arg).
- Stage 44 push/pop lesson: balanced registration + body.

## Files changed

- `src/native.c`: `arrayPartitionNative` + registration.
- `tests/test_stdlib.c`: 8 test functions + `main()` calls.
- `examples/array-partition.lox` (new).
- `docs/stage-62-closeout.md` (this file).

## Next candidates (Stage 63)

1. Modules — Tom's call.
2. browser-harden remainder / heartbeat mechanical tier.
3. Small stdlib gaps if any remain (e.g. `array_flat_map`, `string_pad` already done).
