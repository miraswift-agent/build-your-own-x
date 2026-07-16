# Stage 58 close-out: array_intersect(arr1, arr2, keyFn?) -> array

**Branch:** `stage-58-array-intersect-by-key`
**Date:** 2026-07-16
**Tests:** 468/468 stdlib (was 464 at Stage 57, +4: 4 new Stage 58 test functions)
**vm+clox total:** 508 passed, 0 failed
**Box total:** ~648 (92 allocator + 23 db + 25 shell + 508 vm+clox)
**Valgrind:** clean (exit 0 on all 4 test binaries + the example, 0 errors, 0 leaks)
**Bugs caught:** 0 implementation bugs, 0 test bugs, 0 example bugs in this close-out pass. **15th-consecutive-zero-implementation-bug stage in the new streak** (Stage 44 = 1st, Stage 58 = 15th). The streak is now 1 short of the 16-streak record and 2 short of the 17-streak record.

## What shipped

Extended `array_intersect` from Stage 55 to accept an **optional third argument**: a 1-arg key function. The native is registered under the same name `array_intersect`, so existing 2-arg call sites continue to work unchanged (direct `valuesEqual` comparison), while 3-arg call sites get the new "intersect by key" capability.

Signature: `array_intersect(arr1, arr2, keyFn?) -> array`

- `arr1`, `arr2` must be arrays.
- `keyFn`, if provided, must be a 1-arg closure.
- Multiset semantics by key: for each key k, result count = `min(count_in_arr1 with keyFn=k, count_in_arr2 with keyFn=k)`.
- Order of first appearance in `arr1` is preserved.
- Neither input is mutated.

## The new wrinkle: optional closure argument

Stage 50 established the optional positional parameter pattern (`array_zip_longest(arr1, arr2, fill?)`). Stage 58 applies the same shape, but the optional value is a **closure**, so it adds an `IS_CLOSURE` type check. The pattern is:

```c
ObjClosure *keyFn = NULL;
if (argCount == 3) {
    if (!IS_CLOSURE(args[2])) { /* error */ }
    keyFn = AS_CLOSURE(args[2]);
}
```

This is the first native that combines:
- Optional argument (Stage 50)
- User-code dispatch via `callClosureFromNative` (Stage 30)
- The "remaining copy" set-operation pattern (Stage 55/56/57)

## Performance invariant: cache arr2 keys

A naive implementation would call `keyFn(arr2[j])` inside the inner scan loop, giving `O(|arr1| * |arr2|)` closure calls. The shipped implementation pre-computes a parallel `remaining_keys` array once, reducing total keyFn calls to `O(|arr1| + |arr2|)`. The trade-off is one extra `ObjArray` on the Lox heap, which gets free GC protection via the same push/pop discipline used for `remaining` and `result`.

## Architecture: zero new work

- **Reuses Stage 12** (heap-allocated `ObjArray`s with push/pop GC protection).
- **Reuses Stage 30** (`callClosureFromNative` for 1-arg closure dispatch).
- **Reuses Stage 40** (`valuesEqual()` comparison on computed keys).
- **Reuses Stage 50** (optional positional arg shape).
- **Reuses Stage 55/56/57** (the "remaining copy" set-operation pattern).

The Stage 44 push/pop lesson is applied: the function body pushes `remaining`, optionally `remaining_keys`, and `result`; pops them in reverse order before returning. Registration has 1 push + 1 pop.

## Files changed

- `src/native.c`:
  - Replaced `arrayIntersectNative` with `arrayIntersectByNative` (~120 lines including comments). The 2-arg path is a backward-compatible shim over Stage 55's algorithm; the 3-arg path adds keyFn support.
  - Updated registration in `defineNatives()` to point `array_intersect` at `arrayIntersectByNative`.
- `tests/test_stdlib.c`:
  - Updated `test_array_intersect_wrong_arg_count` comment to reflect that 2 or 3 args are valid, 1 or 4 args error.
  - 4 new test functions: `test_array_intersect_by_key_basic`, `test_array_intersect_by_key_objects`, `test_array_intersect_by_key_omitted`, `test_array_intersect_by_key_wrong_type`.
  - Added corresponding calls in `main()`.
- `examples/array-intersect-by.lox` (new): 3-section example demonstrating object ID intersection, numeric key-class intersection, and 2-arg backward compatibility.
- `docs/stage-58-closeout.md` (this file).

## Tests

- **`test_array_intersect_by_key_basic`**: `[1, 2, 3, 4, 5]` intersected with `[7, 8, 9]` by `x > 2` -> `[3, 4, 5]` (3 true-keys from arr1, arr2 has 3 true-keys, min = 3).
- **`test_array_intersect_by_key_objects`**: `User` objects with `.id` field; intersects `localUsers` with `remoteUsers` by `id`. Result preserves the first matching arr1 element (`alice`).
- **`test_array_intersect_by_key_omitted`**: Verifies the 2-arg form still produces Stage 55 output: `array_intersect([1,2,3,2,1], [2,3,4,2]) = [2,3,2]`.
- **`test_array_intersect_by_key_wrong_type`**: 3rd arg that is not a closure (e.g., `99`) is a runtime error.

## Example verification

```
=== array_intersect by key: objects ===
shared.len:1
alice
=== array_intersect by key: numbers ===
len:3
3
4
5
=== array_intersect 2-arg form still works ===
len:3
2
3
2
```

## Next decision

The set-operations family is complete (intersect, union, difference), and the first parameterized extension (intersect-by-key) is now on disk. Candidates for Stage 59:
- `array_union(arr1, arr2, keyFn?)` — extend Stage 56 symmetrically.
- `array_difference(arr1, arr2, keyFn?)` — extend Stage 57 symmetrically.
- `array_slice(arr, start, end?)` — 3-arg 1-D slice.
- `string_pad_end(s, n, char?)` — like Stage 22 but pads at the end.
- **Modules** — ~600 lines, Tom's call.

Tom has redirect rights at the next heartbeat.
