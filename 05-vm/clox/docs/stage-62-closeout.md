# Stage 62 close-out: array_partition(arr, predicate) -> array

**Branch:** `stage-62-array-partition`
**Date:** 2026-07-19
**Tests:** 499/499 stdlib (was 489 at Stage 61; +10 assert-level passes across 8 test functions)
**vm+clox total:** 539 passed, 0 failed (27 clox + 5 repl + 499 stdlib + 8 composability)
**Box total:** ~679 (92 allocator + 23 db + 25 shell + 539 vm+clox)
**Valgrind:** clean (exit 0 on all 4 test binaries, 0 errors, 0 leaks; stdlib 3084/3084 allocs/frees)
**Bugs caught:** 0 implementation bugs. 1 test-author fix pre-pass (evenness via `n-(n/2)*2` is wrong under clox doubles — switched to `n > 3`; avoided `string()` on non-numbers). **19th-consecutive-zero-implementation-bug stage** (Stage 44 = 1st, Stage 62 = 19th). **NEW RECORD**.

## What shipped

New native `array_partition(arr, predicate) -> array`. One-pass HO split into `[pass, fail]` (lodash `_.partition` shape). Both sides preserve source order. Empty source → `[[], []]`. Non-mutating. Same truthiness as `array_filter` (only `false` and `nil` are falsy).

## The new wrinkle: two result arrays under one outer array

Three heap arrays GC-protected while the predicate runs (`pass`, `fail`, `outer`). Not just filter + inverse-filter: one pass, one dispatch cost per element, pair is the unit.

## Architecture: zero new work

- Reuses Stage 12 (heap ObjArray + push/pop GC)
- Reuses Stage 30 (`callClosureFromNative` 1-arg)
- Reuses Stage 30 truthiness rule
- Stage 44 push/pop: push pass, fail, outer; pop reverse; registration 1+1

## Files changed

- `src/native.c`: `arrayPartitionNative` + registration
- `tests/test_stdlib.c`: 8 test functions + main()
- `examples/array-partition.lox` (new)
- `docs/stage-62-closeout.md` (this file)

## Next candidates (Stage 63)

1. Grow trigger-mine (browser-harden remainder / heartbeat mechanical) — HO/stdlib mirrors are thinning
2. Small stdlib leftovers if any remain useful
3. **Modules** — Tom's call
