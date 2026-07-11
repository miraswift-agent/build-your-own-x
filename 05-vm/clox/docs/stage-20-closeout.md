# Stage 20 Close-out — `array_push` Returns the New Length

**Author:** Mira
**Date:** 2026-07-11
**Branch:** `stage-19-array-reverse` (extended, not forked)
**Commits:** `91d63c8` (impl + 4 tests)

## What this stage is

`array_push(arr, val)` now returns the new length of the array
(as a number) instead of nil. This is a convention refinement:
the "return the natural value" convention introduced in Stage 19
is now applied to `array_push` as well.

## Why this stage

The Stage 19 close-out named this as the Stage 20 pick. The
reasoning:

1. **Consistency.** Stage 19 introduced the convention "return
   the natural value, not nil." `array_reverse` follows it
   (returns the array). `array_push` didn't (returned nil).
   Inconsistency between two mutator natives is friction for
   users.
2. **Natural return value.** For a push, the natural return
   value is the new length. The user often wants to know the
   new length after pushing (e.g., to index the new element,
   or to check the array isn't growing unboundedly).
3. **Cheap to do.** The change is one line in the impl. No
   new code path, no new GC concern, no new error semantics.
   The cost is essentially zero.
4. **Backward-compatible.** Existing code that calls
   `array_push(a, x);` and ignores the return value is
   unaffected. The return value is now a number instead of
   nil, but the side effect is the same.

## What I built

### Implementation (`src/native.c`)

The change is one line:

```c
/* Before: */
arrayPush(AS_ARRAY(args[0]), args[1]);
return NIL_VAL;

/* After: */
ObjArray *array = AS_ARRAY(args[0]);
arrayPush(array, args[1]);
return NUMBER_VAL((double)array->count);
```

The `array` variable is captured so we can read the count
*after* the push (the push increments the count). The
`NUMBER_VAL` macro wraps the count as a clox number value.

### Tests (4 new tests)

| Test | What it checks |
|------|----------------|
| `test_array_push_returns_new_length` | `n = array_push(a, 4)` sets `n` to 4 (the new length) |
| `test_array_push_returns_length_growing` | Each push returns the *new* (larger) length, growing 1, 2, 3 |
| `test_array_push_return_value_is_number` | The return is a number — can do `n + 10`, `n - 3` on it |
| `test_array_push_chained_returns` | Repeated `n = array_push(...)` updates `n` each time, agrees with `array_length` |

The existing `test_array_push` (which doesn't use the return
value) is unaffected — it still passes.

## Design decision: return the new length (not the new element)

The convention "return the natural value" has two candidates
for `array_push`:

- **Return the new length** (chosen): the new length is the
  *new* state, not just the new element.
- **Return the new element** (rejected): the new element is
  already in `args[1]` — the user has it. Returning it would
  be redundant unless they're using `array_push` in an
  expression where they don't have a handle to the element.
  Even then, `a[array_length(a) - 1]` is two operations, not
  one, and the user could just bind the element first.

I chose the new length because:

1. **It's the new state.** After a push, the array is "one
   longer." The length is the new state; the new element is
   just one of N elements. Returning the length captures
   *the change*; returning the element captures *one of the
   new things*.
2. **It's not redundant.** The user does have `args[1]` if
   they passed it. They don't have the new length without
   calling `array_length` separately.
3. **It composes with the array_reverse pattern.** If the
   user wants the new element, they can do
   `array_get(a, array_push(a, x) - 1)`. This is one
   expression that gives the just-pushed element.

## Bugs caught

**Zero.** The convention is small enough that the tests
passed on the first run after the impl. This is the **11th
consecutive stage with zero implementation bugs**.

The Stage 20 design call was made *before* the test was
written (in the Stage 19 close-out), so the test contract
was clear. The test-first discipline held: 4 new tests, 4
expected failures, 4 actual passes after the 1-line change.

## Verification

```
$ cd 05-vm/clox && make clean && make
[no warnings under -Wall -Wextra -std=c99 -pedantic]

$ make test
==> bin/test_clox
27 passed, 0 failed   (unchanged)
==> bin/test_composability
8 passed, 0 failed
==> bin/test_repl
5 passed, 0 failed
==> bin/test_stdlib
119 passed, 0 failed   (+4 from Stage 19's 115)

$ make valgrind
[0 errors, 0 leaks on all four binaries]
```

Total: **159/159 tests pass** (27 clox + 5 repl + 119 stdlib
+ 8 composability), valgrind clean on all four binaries.

### All 10 example programs pass `make examples`

The existing examples don't use the return value of
`array_push`, so they all still work without modification.

## What this stage teaches

**Convention calls compound.** Stage 19 introduced "return the
natural value, not nil" with `array_reverse`. Stage 20 applies
it to `array_push`. The discipline: when a convention is
introduced, *audit the existing code for places that don't
follow it and fix them in small, separate stages.* This is
much cheaper than fixing the convention retroactively, and
it makes the codebase internally consistent.

The same discipline applies to other conventions I've
introduced:

- **Strict mode for escape errors** (Stage 18). Are there
  other places where the language is lenient that should be
  strict? Not currently — escape processing was the only
  lenient case.
- **In-place vs new-array for mutators** (Stage 19). Are
  there other mutators that should be in-place?
  `array_pop` is read-only (returns the popped value), not a
  mutator. `array_set` is in-place (mutates the array).
  `array_reverse` is in-place. Consistent.
- **Per-element allocation for new natives** (Stages 13-19).
  All new natives use the existing ObjArray internals;
  no new heap types added since Stage 12.

**Tiny stages are real stages.** Stage 20 is a 1-line change.
It still counts as a stage because:
1. It's a real behavior change (return value is now a number,
   not nil).
2. It has its own tests.
3. It has a close-out that documents the design call.
4. It maintains the two-commit rhythm and the push-to-origin
   discipline.

The alternative — bundling it into Stage 19 — would have
broken the "one stage, one design call" rule and made the
Stage 19 close-out harder to read. Small stages are easier
to review, easier to revert, and easier to learn from.

## A note on what I did NOT add

- **A flag to opt out of the new return value.** No
  backward-incompatibility flag. The change is non-breaking
  (existing code that ignores the return is unaffected), so
  no opt-out is needed.
- **A deprecation warning for the nil return.** Not possible
  in clox — there's no static type system to warn on, and
  the return value is just a Value at runtime.
- **A way to get the new element from the return value.**
  Already covered: `a[array_push(a, x) - 1]`. Not
  load-bearing.

## What's next

From the Stage 17 close-out, the remaining "Stage 15 costs
documented" gaps are:

1. ~~`string(n)`~~ ✅ Stage 17
2. ~~escape sequences~~ ✅ Stage 18
3. ~~hand-rolled reverse~~ ✅ Stage 19
4. **hand-rolled filter** — needs user-code dispatch
5. **string+number concat** — needs compiler change

The remaining 2 are correctly deferred:
- **`array_filter`** needs user-code dispatch, which is a
  bigger architectural change (likely a stdlib-in-lox stage).
- **`string+number` concat** needs a compiler change (the
  `+` operator currently requires matching types; allowing
  `string + number` would be a new opcode path).

**My pick for Stage 21**: **`string_repeat(s, n) -> string`**
or **`string_pad_start(s, width, fill) -> string`** — both
close small gaps in the string-handling idiom. ~25 lines
each, no new concepts, fully reversible.

Alternatively, if the user wants a bigger swing:
**modules** is still the next big thing (~600 lines).

## Closing observation

Stage 20 is the smallest stage in the project: 1 line of
implementation, 4 tests, ~107 lines of total diff (most of
which is the close-out and tests). The discipline: when a
change is small enough to be a stage, it should be a stage.
The cost of a stage is the close-out doc and the
push-to-origin; both are cheap.

The bug trajectory continues to improve: 11 consecutive
stages with zero implementation bugs. The discipline of
"design call in the close-out *before* writing the test, then
test-first impl" is the mechanism. The design call in the
Stage 19 close-out was: "convention refinement, return new
length, ~5 lines, fully reversible." That design call made
the Stage 20 test contract obvious, and the implementation
followed cleanly.

The remaining Stage 15 costs (filter, string+number concat)
are correctly deferred to bigger stages. The discipline of
documenting the limitations is paying off: each deferred
item has a concrete next-step design, and each closed item
was small enough for a single stage.
