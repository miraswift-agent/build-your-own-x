# Stage 21 — string_repeat

**Completed:** 2026-07-14
**Branch:** `stage-21-string-repeat`
**Commits:** `0069eda` (impl + tests + example)
**Tests added:** 5 (basic, n=0, n<0 error, wrong-type error, wrong-arity error)
**Test aggregate:** 124/124 (was 119)
**Valgrind:** 0 errors, 0 leaks, 768 allocs / 768 frees in test_stdlib

## What landed

`string_repeat(s, n) -> string` — concatenate `s` with itself `n` times.

- `string_repeat("ha", 3)` → `"hahaha"`
- `string_repeat("x", 5)` → `"xxxxx"`
- `string_repeat("hello", 0)` → `""`
- `string_repeat("abc", 1)` → `"abc"`
- `string_repeat("x", -1)` → runtime error
- `string_repeat(42, 3)` → runtime error
- `string_repeat()` → runtime error (wrong arity)

## Design decisions

- **`n = 0` → `""`.** Python convention. The "repeat zero times" idiom is
  well-defined, and forcing a special case for it would be worse.
- **Negative `n` → runtime error.** A negative count is nonsensical.
  Matches `string_substring`'s discipline of rejecting bad numeric input
  as a runtime error rather than returning something nonsensical.
- **Fractional `n` → truncated to int.** Matches `string_substring`'s
  `(int)` cast. Documented in the comment so a future reader knows the
  behavior is intentional, not accidental.
- **Overflow guard.** `s->length * n > INT_MAX` is a runtime error.
  2 GiB of string is well beyond any realistic Lox use case, and silent
  integer wrap is the kind of bug that's catastrophic and invisible.
  Same discipline as Stage 11's "bounded buffers are mandatory," applied.
- **Single allocation.** One `ALLOCATE(char, outLen + 1)`, `n` memcpy
  passes, one `copyString` call, one `FREE_ARRAY`. No intermediate
  `ObjString`s, no GC pressure in the loop, single interned result.
  This is the same shape as `string_join`.

## Example: examples/repeat-divider.lox

```lox
var label = "STAGE 21 COMPLETE";
print(label);
print(string_repeat("=", string_length(label)));
print("string_repeat: 124/124 tests pass, valgrind clean");
```

The natural "match the divider length to the label" idiom that would
otherwise need a hand-rolled loop or a hardcoded width. Stage 21
closes the gap. (11 example programs total now.)

## Bugs caught

**0 implementation bugs.** Tests written first, the contract was clear,
the implementation matched, valgrind was clean on the first run.
**12th consecutive stage with zero implementation bugs.**

## Stage 15 "costs documented" status

The four language limitations from Stage 15's composability tests were:

1. ✅ `string(n)` — closed in Stage 17
2. ✅ escape sequences in string literals — closed in Stage 18
3. ✅ hand-rolled reverse loop — closed in Stage 19 (`array_reverse`)
4. ⏳ hand-rolled filter loop — deferred (needs user-code dispatch)

`string_repeat` is **not** a Stage 15 closure; it was a Stage 21 pick
from the wiki INDEX's "what's next" list. The composability tests
surfaced the reverse / repeat / filter pattern as common idioms; Stage
19 closed reverse, Stage 21 closes repeat, filter remains deferred.

The separately-listed `string+number` concat (compiler change) is also
still deferred.

## What this stage teaches

*Closing-the-gap primitives keep the rhythm healthy.* Stage 17 closed
`string(n)`, Stage 18 closed escape sequences, Stage 19 closed
`array_reverse`, Stage 21 closes `string_repeat`. Each was a small
change that unblocked an obvious idiom in the user's programs. The
"open question of what to build next" stays small because the answers
fall out of the composability tests and the prior close-outs. The
shape of a healthy project.

The single-allocation / n-memcpy-passes / single-copyString discipline
is the *implementation* shape that comes from `string_join`. The Stage
21 implementation is structurally a special case of `string_join`
where the array elements are all the same string. Reading them
side-by-side, the same allocation pattern shows up — that's the
plumbing becoming idiomatic.

## My pick for Stage 22

Wiki's candidates (per the INDEX):

1. `string_pad_start(s, width, fill) -> string` — same shape as
   `string_repeat`, ~30 lines, no new concepts. The natural next
   small primitive.
2. `array_filter(arr, predicate)` — needs user-code dispatch from a
   native, ~150 lines, real architecture. Defer until the small
   primitives run out.
3. Modules — ~600 lines, the next big swing. Tom's call, not mine.

**My pick:** `string_pad_start` next, for the same reasons as Stage
21. Keep the rhythm small until there's a user need that breaks it.

## Cross-references

- `[[../wiki/build-your-own-x#INDEX]]` — the front matter with current
  state, candidates, and decision points.
- Stage 19 close-out — `array_reverse` followed the same pattern:
  small primitive, single-file diff, valgrind clean.
- Stage 17 close-out — `string(n)`, the first "closing the gap" stage
  in this arc.
