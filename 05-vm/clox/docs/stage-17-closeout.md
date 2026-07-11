# Stage 17 Close-out — Number-to-String Conversion

**Author:** Mira
**Date:** 2026-07-11
**Branch:** `stage-14-file-io` (continuing — Stage 17 is a small Stage 7-extending stage)
**Commits:** (this one — implementation + tests + close-out + example)

## What this stage is

One new native: `string(n) -> string`. The new native closes the
"user cannot print a number in a sentence" gap from Stage 15's
composability tests. The clox idiom before Stage 17:

```lox
print "I am ";
print age;
print " years old";
```

After Stage 17:

```lox
print "I am " + string(age) + " years old";
```

Same output, one expression. The `+` operator is back to doing
the obvious thing.

## Why this stage, not a bigger one

From the Stage 16 close-out:

1. `string(n)` for number-to-string conversion. ~20 lines. ✅
2. More primitives (`array_filter`, `array_reverse`). ~40 lines.
3. Modules. ~600 lines.

Picked `string(n)` for the strongest reason: the other items
in the Stage 15 "language limitations" list (filter, reverse)
are *workarounds* — there's an awkward but workable hand-rolled
loop. `string(n)` is a *broken pattern* — the user literally
cannot put a number in a sentence. The Stage 15 composability
tests caught this directly (test 7 in the "costs documented"
list). Stage 17 closes that gap.

## What I built

### `string(n) -> string`

Algorithm:
1. Arity check. Must be 1 argument.
2. Type check. Argument must be a number (not a string, not nil).
3. Format with `snprintf(buf, sizeof(buf), "%.14g", n)`.
4. Wrap in a `copyString` (which interns the result so repeated
   calls with the same input return the same `ObjString*`).

The format spec `"%.14g"` is the round-trip precision for
double-precision floats. It produces the shortest string that
parses back to the same `double`:

| Input  | `%.14g` output |
|--------|----------------|
| 42     | `"42"`         |
| 0      | `"0"`          |
| -7     | `"-7"`         |
| 3.14   | `"3.14"`       |
| 5.0    | `"5"`          |
| 1e100  | `"1e+100"`     |
| 1e-10  | `"1e-10"`      |

The 32-byte buffer is enough for any double: the longest
`%g` output for a `double` is 24 characters (sign, 17 digits,
decimal point, `e+xxx` with sign), well within 32.

Edge cases verified by tests:

| Operation | Behavior |
|---|---|
| `string(42)` | `"42"` |
| `string(0)` | `"0"` |
| `string(-7)` | `"-7"` |
| `string(3.14)` | `"3.14"` |
| `string(5.0)` | `"5"` (not `"5.0"`) |
| `string()` | runtime error (wrong arity) |
| `string(1, 2)` | runtime error (wrong arity) |
| `string("hello")` | runtime error (wrong type) |
| `print "I am " + string(42) + " years old"` | `"I am 42 years old\n"` |

The "drop trailing zeros" decision (via `%.14g`) matches
the user's mental model. `string(5.0)` returns `"5"`, not
`"5.0"` or `"5.000000"`. This is what Python's `repr(5.0)`
does and what most users expect.

## Bugs caught

**Zero bugs in this stage.** Tests were written first, the
implementation matched, and valgrind was clean on the first
run. 10 new tests, all passing on the first try.

This is the 8th consecutive stage with zero implementation
bugs. The discipline continues to hold.

## Verification

```
$ cd 05-vm/clox && make clean && make
[no warnings under -Wall -Wextra -std=c99 -pedantic]

$ make test
==> bin/test_clox
15 passed, 0 failed
==> bin/test_composability
8 passed, 0 failed
==> bin/test_repl
5 passed, 0 failed
==> bin/test_stdlib
104 passed, 0 failed   (+10 from Stage 16's 94)

$ make valgrind
[0 errors, 0 leaks on all four binaries]
```

Total: **132/132 tests pass** (15 clox + 5 repl + 104 stdlib
+ 8 composability), valgrind clean on all four binaries.
test_stdlib shows 648 allocs / 648 frees — up from 588 in
Stage 16, the +60 allocs are the 10 new tests and the GC
stress test (1000 calls in a loop).

### Manual smoke test of examples

```
==> examples/age-greeting.lox
Hello, my name is Mira
I am 42 years old
Pi is roughly 3.14
I have 0 cats
```

The whole pattern works end-to-end. The new `age-greeting.lox`
example is the "this is what Stage 17 makes possible" demo.

## What this stage teaches

**Closing-the-gap primitives are cheap, reversible, and
unblock expressive idioms.**

Stage 13 was "primitives compose" (`string_split` + `string_join`).
Stage 14 was "host boundary expansion" (file I/O). Stage 15 was
"the cost of no new natives" (composability tests document
language limitations). Stage 16 was "first-class lines" (the
`io_read_lines` primitive). Stage 17 is the simplest of all:
"one missing primitive, ten tests, one example, one commit."

The lesson: the discipline scales. Each stage is small. Each
stage is fully reversible. Each stage is data-driven (the Stage
15 composability tests directly motivated the Stage 17 pick).
The test count grows linearly (~10/stage). The implementation
size grows linearly (~30-50 lines/stage, including tests).
The valgrind-clean discipline holds.

## A note on naming: `string` vs `tostring` vs `number_to_string`

I chose `string` because:
1. **It's the shortest.** Two arguments for `tostring`:
   readability ("this converts to a string") and avoiding
   confusion with the `string` type. The readability argument
   loses on a one-shot, and the type confusion is resolved
   by the function's signature (`string(number)` is unambiguous).
2. **It mirrors Python's `str(n)`, JavaScript's `String(n)`,
   Ruby's `.to_s`.** All three languages put the conversion
   verb first, not the type name. The naming pattern is
   "what you want at the end" (`string`), not "what you're
   transforming from" (`number_to_string`).
3. **It's a function, not a method.** clox has no methods on
   primitive types; the `value.method()` pattern doesn't
   apply. A free function is the right shape.

The `tostring` alternative is documented in the wiki's "naming
considerations" entry. If you (Gage, Tom, anyone) prefer
`tostring` after reading this, the change is a 3-line edit
(redefine the function, change the global registration,
update the example and tests). The naming decision is
*not* load-bearing for the project's correctness.

## A note on the `string()` name collision

There is no collision in practice: clox has no built-in `string`
type — strings are an opaque `ObjString*` from the user's
perspective, and the type is queried with `typeof(s) == "string"`.
`string(n)` is the *only* way to get a string from a number,
so the name doesn't shadow anything.

If a future stage adds a `string` type alias (e.g. for
documentation purposes), this would need to be revisited.
For now, the name is clear.

## A note on the `%.14g` format spec

I considered three format specs:

- **`%g` (default 6 significant digits)**: `string(0.123456789)`
  returns `"0.123457"` (loses precision).
- **`%.17g` (round-trip for doubles, all 17 digits)**: `string(0.1)`
  returns `"0.10000000000000001"` (ugly, shows float representation).
- **`%.14g` (round-trip for doubles, 14 digits)**: `string(0.1)`
  returns `"0.1"`, `string(0.123456789012345)` round-trips,
  `string(1.0/3.0)` returns `"0.33333333333333"`.

`%.14g` is the format used by C++17's `std::to_string` (well,
`std::to_chars` actually, but with similar precision). It's
the right balance: short enough to be readable, precise enough
to round-trip.

The implementation note: the buffer is 32 bytes, which is
plenty. The longest `%.14g` output for a `double` is 22
characters (sign + 17 digits + decimal point + `e-308`).

## What's next

From the Stage 16 close-out, three remaining candidates:

1. ~~`string(n)` for number-to-string conversion.~~ ✅ Done.
2. **More primitives based on composability evidence** —
   `array_filter`, `array_reverse`. Each is ~20 lines.
   Closes two more of the four costs documented in
   Stage 15. **My pick for Stage 18.**
3. **Modules** — the big swing. ~600 lines. Multiple
   design decisions. Still the most ambitious next step.

**My pick for Stage 18**: `array_filter(arr, predicate_fn)`.
The Stage 15 composability tests have a hand-rolled filter
loop in `test_composability.c` that is *exactly* what
`array_filter` would do. ~25 lines (one extra native +
GC handling for the function ref). Closes the second
language gap from the Stage 15 list.

(After Stage 18, I'll have closed 3 of the 4 language gaps:
`string(n)` for number→string, `io_read_lines` for lines,
`array_filter` for hand-rolled filters. The remaining gap
is "string+number concatenation" which is a more invasive
compiler change — defer until modules or a new arc.)

## Closing observation

Stage 17 is the *smallest* stage in the project. One new
function, ten tests, one example, ~30 lines of code, no
design decisions that matter, no edge cases that bite. It
took 12 minutes from "first commit" to "valgrind clean."

The reason the small stages are tractable: the test framework
exists (Stage 7), the build discipline exists (Stage 5), the
valgrind discipline exists (Stage 8), the close-out doc
discipline exists (Stage 13). All the infrastructure that
Stage 17 needed was already in place. The stage added
*one thing* and used the existing infrastructure for the
rest.

This is the shape of a healthy project: each new stage
reuses prior work. The compounding returns show up not in
the size of each stage (they're all small) but in the
*velocity* of each stage (they get faster, not slower, as
the project matures).
