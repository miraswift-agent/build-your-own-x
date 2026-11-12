# Stage 63 close-out — `array_flat_map(arr, mapper) -> array`

**Date:** 2026-07-20  
**Branch:** `stage-63-array-flat-map`  
**Streak:** 20th consecutive zero-impl-bug (Stages 45–63; Stage 44 broke prior streak)

## Spec

- Signature: `array_flat_map(arr, mapper) -> array`
- `mapper` is a 1-arg Lox closure
- For each element, call mapper; if the result is an array, append its elements (1 level); else append the value
- Does **not** recurse into nested arrays inside mapped arrays
- Does **not** mutate source
- Empty source → `[]`; empty mapped arrays contribute nothing
- Errors: wrong argc, non-array source, non-function mapper, wrong arity

## Why a native

Not just `array_flatten(array_map(...))`:
1. One pass without a long-lived intermediate nest of arrays
2. Mixed scalar/array mapper returns are first-class
3. Standard HO primitive (JS `flatMap` / lodash `_.flatMap`) sitting next to map/filter/partition

## Wrinkle

Map + 1-level flatten with **unknown result length** — capacity starts at `src->count` (scalar lower bound); `arrayPush` grows as needed. Mapped arrays are temporarily pushed for GC while copying out.

## Tests

11 new stdlib asserts (two error cases each emit 2 pass/fail checks → 9 functions, 11 pass counts):
- basic pair expansion
- scalar mapper (= map)
- mixed scalar/array
- empty source
- empty inner drops
- no deep recurse
- no mutate
- wrong argc / wrong type

**Results:** 510/510 stdlib; vm+clox 550 (27+5+8+510); valgrind clean 3150/3150 allocs/frees on test_stdlib; example clean.

## Example

`examples/array-flat-map.lox`

## Lessons

- Float division makes naive "even" checks wrong in examples (`x - (x/2)*2` is always 0 for reals). Prefer explicit equality when demoing drop-outs.
- GC: push the mapped array while iterating its elements; same discipline as any temporary Obj* from user code.
