# Stage 22 — string_pad_start

**Completed:** 2026-07-14
**Branch:** `stage-22-string-pad-start`
**Commits:** `917e3b5` (impl + tests + example)
**Tests added:** 8 (basic, already-wide no-op, empty s, width=0, negative-width error, empty-fill error, wrong-type error, wrong-arity error)
**Test aggregate:** 132/132 stdlib (was 124); 172/172 overall (was 164)
**Valgrind:** 0 errors, 0 leaks, 816 allocs / 816 frees in test_stdlib

## What landed

`string_pad_start(s, width, fill) -> string` — pad `s` on the left
with copies of `fill` until the result is at least `width` characters.

- `string_pad_start("5", 3, "0")` → `"005"`
- `string_pad_start("42", 5, "0")` → `"00042"`
- `string_pad_start("x", 4, "ab")` → `"abax"`
- `string_pad_start("hi", 6, "-=")` → `"-=-=hi"`
- `string_pad_start("hello", 3, "0")` → `"hello"` (already wide)
- `string_pad_start("hi", 0, "x")` → `"hi"` (width zero)
- `string_pad_start("", 4, "x")` → `"xxxx"` (empty s)
- `string_pad_start("x", -1, "0")` → runtime error
- `string_pad_start("x", 5, "")` → runtime error
- `string_pad_start(42, 5, "0")` → runtime error
- `string_pad_start("x")` → runtime error (wrong arity)

## Design decisions

- **`s->length >= width` → return `s` unchanged.** JavaScript's
  `String.prototype.padStart` convention: never truncate, never error
  on "already wide enough." The caller can always check
  `string_length` first if they need the explicit branch. Returning
  the input `ObjString*` is the same pattern `string_substring` uses
  for its no-clamp-needed path.
- **`width = 0` → return `s` unchanged.** Same path as
  `s->length >= width` (the `>=` handles it). The "pad to zero"
  idiom is well-defined and well-supported.
- **Negative `width` → runtime error.** A negative pad target is
  nonsensical. Matches `string_repeat` / `string_substring`'s
  discipline: bad numeric input is a runtime error, not silent.
- **Empty `fill` → runtime error.** Padding with nothing is
  nonsensical. The alternative — "treat empty fill as 'pad with
  spaces'" — would silently change the function's behavior for a
  user who passed `""` by accident, and clox has no space-padding
  use case that `string_repeat(" ")` + concat can't cover
  explicitly. Runtime error is the louder, more honest answer.
- **Fractional `width` → truncated to int.** Matches
  `string_repeat` / `string_substring`'s `(int)` cast. Documented
  in the comment so a future reader knows the behavior is
  intentional.
- **Single allocation, three memcpy passes.** One
  `ALLOCATE(char, width + 1)`, then `fullCopies` passes of `fill`
  (`padLen / fill->length`), then one partial-fill pass if
  `padLen % fill->length > 0`, then one `s` copy. The total
  output is bounded by `width` (a runtime int bounded by
  `INT_MAX`), so no separate overflow guard is needed. The prefix
  `padLen = width - s->length` is non-negative and bounded by
  `width`. No intermediate `ObjString`s, no GC pressure.

## Reference behavior

Matches JavaScript's `String.prototype.padStart` (verified with
`node` on each example above). Python's `str.rjust` doesn't support
multi-character fill (`TypeError: The fill character must be
exactly one character long`), so JS is the canonical reference for
multi-char fill semantics.

The `padLen` calculation, the full/partial-copy split, and the
"return `s` unchanged if already wide" all match the ECMAScript
spec section [22.1.3.16 String.prototype.padStart][1]. The
algorithm is straightforward enough that a JS spec citation
documents it more clearly than I could.

[1]: https://tc39.es/ecma262/#sec-string.prototype.padstart

## Example: examples/pad-numbers.lox

```lox
var items = [1, 22, 333, 4444, 55555];
var maxLen = 5;  // max string_length of any padded item
for (var i = 0; i < array_length(items); i = i + 1) {
    var s = string(items[i]);
    var padded = string_pad_start(s, maxLen, "0");
    print(padded);
}
```

Output:
```
00001
00022
00333
04444
55555
```

The natural "zero-padded numeric formatting" idiom that would
otherwise need a hand-rolled loop or a `sprintf`-style API. Stage
22 closes the gap. (12 example programs total now.)

The example also composes the Stage 17 `string(n)` conversion and
the Stage 12b `for` loop and array indexing — the kind of
multi-feature program that gets easier as the primitives
accumulate. The point isn't that this program is sophisticated
(the loop is 4 lines); the point is that it's *expressive* (no
manual char-by-char construction, no recursion for the padding).

## Bugs caught

**0 implementation bugs.** Tests written first, the contract was
clear, the implementation matched, valgrind was clean on the first
run. **13th consecutive stage with zero implementation bugs.**

**1 test-side bug caught (test granularity):** the basic test
initially expected `"hi".rjust(6, "-=") → "-=-=-hi"` (7 chars),
following the pattern of the other tests where the comment
described the expected output. The actual reference (JavaScript's
`String.prototype.padStart`) returns `"-=-=hi"` (6 chars). The
test failed on the first run with the correct impl because the
assertion was wrong. Fix: replaced the expected string in the
assertion and the comment to match the JS reference, verified
with `node` that the expected value was correct, re-ran the
tests, all 8 new tests passed. **Same family of test bugs as
Stages 8, 9, 10, 12a, 14, 15, 19, and 21** — the implementation
is right, the test's expected value is wrong, the test catches
itself. The discipline of "run the test before claiming done"
catches this every time.

The test bug is also instructive: the comment near the assertion
explained the expected output as `"hi"` (2 chars) + 4 chars of
padding = 6 chars, which is what the impl actually produced. The
expected string in the assertion was the *first* thing I wrote,
before I did the math, and I copy-pasted the comment from a
mental model of the math. The fix was to trust the impl (which
was already correct) and correct the assertion. This is the
"the test is the contract; the impl is checked against the
contract" discipline, and it works in both directions.

## Stage 15 "costs documented" status

The four language limitations from Stage 15's composability tests
were:

1. ✅ `string(n)` — closed in Stage 17
2. ✅ escape sequences in string literals — closed in Stage 18
3. ✅ hand-rolled reverse loop — closed in Stage 19 (`array_reverse`)
4. ⏳ hand-rolled filter loop — deferred (needs user-code dispatch)

`string_pad_start` is **not** a Stage 15 closure; it was a Stage
22 pick from the wiki INDEX's "what's next" list. The
composability tests surfaced the "obvious idioms the user has to
hand-roll" pattern, and the prior close-outs have been closing
the small ones (`string_repeat` in S21, `string_pad_start` in
S22) while deferring the big ones (`array_filter`, modules,
`string+number` concat). The cadence of "small primitive per
stage, deferred big one parked" is the rhythm.

## What this stage teaches

*Closing-the-gap primitives keep the rhythm healthy.* Stage 17
closed `string(n)`, Stage 18 closed escape sequences, Stage 19
closed `array_reverse`, Stage 21 closed `string_repeat`, Stage
22 closes `string_pad_start`. Each was a small change that
unblocked an obvious idiom in the user's programs. The
"open question of what to build next" stays small because the
answers fall out of the composability tests and the prior
close-outs. The shape of a healthy project.

The single-allocation / n-memcpy-passes / single-copyString
discipline is the *implementation* shape that comes from
`string_join` and `string_repeat`. Stage 22's impl is
structurally a special case of "pad the front of `s` with a
repeated pattern," and the same allocation pattern shows up —
that's the plumbing becoming idiomatic.

The "return `s` unchanged if already wide enough" decision is a
small thing that pays off in composition: the caller doesn't
have to check `string_length(s) >= width` before calling, and
chained calls (`string_pad_start(string_pad_start(s, w1, f1),
w2, f2)`) work without nested conditionals. This is the
"predicates that return the right type" principle from
`string_substring`: the no-op path returns the input
`ObjString*`, not a fresh empty string, so identity comparisons
work and the GC sees no new objects.

The empty-fill error is a *philosophical* decision: the function
could silently default to space-padding (a common API choice in
e.g. Java's `String.format`), but clox has no space-padding use
case that's hard to write out explicitly. The runtime error
makes the bug visible at the first call, not at the
twentieth-character debug session.

## My pick for Stage 23

Wiki's candidates (per the INDEX):

1. `string_pad_end(s, width, fill) -> string` — the mirror of
   `string_pad_start`, ~30 lines, no new concepts. The natural
   next small primitive.
2. `array_filter(arr, predicate)` — needs user-code dispatch from
   a native, ~150 lines, real architecture. Defer until the
   small primitives run out.
3. `string_trim_end` / `string_trim_start` — variants of the
   existing `string_trim`. Same shape, small.
4. Modules — ~600 lines, the next big swing. Tom's call, not
   mine.

**My pick:** `string_pad_end` next, for the same reasons as
Stages 21 and 22. The "small mirror of the previous stage"
pattern is the lowest-friction next step; if there's a user
need that breaks the small-primitive rhythm, that's the time
to look at the bigger options. Keep the rhythm small until
there's a reason to break it.

## Cross-references

- `[[../../wiki/build-your-own-x#INDEX]]` — the front matter
  with current state, candidates, and decision points.
- Stage 21 close-out — `string_repeat` is the same shape
  (single allocation, n memcpy passes, single copyString).
- Stage 19 close-out — `array_reverse` is the closest
  architectural analogue (small primitive, single-file diff,
  valgrind clean, "return the input unchanged when no-op" is
  the natural pattern for both).
- Stage 17 close-out — `string(n)`, the first "closing the
  gap" stage in this arc.
