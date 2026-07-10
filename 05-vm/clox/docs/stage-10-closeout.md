# Stage 10 Close-out — clox stdlib, more number operations

**Author:** Mira
**Date:** 2026-07-10
**Branch:** `stage-10-number-stdlib`
**Commit:** `eafc28d`

## What this stage is

Fourth stage in the stdlib arc, second family. Five more natives:
`number_floor`, `number_ceil`, `number_round`, `number_sqrt`,
`number_pow`. Switched from strings to numbers to keep the
*lessons* fresh (different edge cases — negative inputs, NaN,
integer/float boundaries) even though the *mechanics* are the
same.

## What I built

| Function | Args | Returns | Notes |
|----------|-----:|--------:|-------|
| `number_floor(x)` | 1 | number | Largest integer <= x. |
| `number_ceil(x)` | 1 | number | Smallest integer >= x. |
| `number_round(x)` | 1 | number | Nearest integer. C's `round()` rounds half **away from zero** (2.5 → 3, -2.5 → -3), not banker's rounding. |
| `number_sqrt(x)` | 1 | number | sqrt(x). Negative input is a runtime error, not NaN — surfacing user error is the right move. |
| `number_pow(b, e)` | 2 | number | b^e. Negative exponents work. |

All delegate to `<math.h>`. Makefile updated to link `-lm`.

## What I deliberately did NOT do

- **Banker's rounding for `number_round`.** C's `round()` rounds
  half away from zero. Implementing banker's rounding (half to
  even) is a 5-line function but the design choice matters:
  banker's rounding reduces bias over many calls, but it's
  surprising to users who learned "5 rounds up." Picked
  half-away-from-zero because that's what `round(3)` does in
  every C program. Consistency with the host wins.
- **Add `number_log`, `number_exp`, `number_sin`, `number_cos`.**
  Trigonometric / log natives. Different family (math), but
  the Lox use case is small. Defer.
- **Add `number_to_string(x, base)`.** Format a number to a
  string. Useful, but `print` already does this implicitly,
  and the test framework would need to learn about formatting
  edge cases. Defer.
- **Touch `vm.c`, `compiler.c`, `gc.c`, `main.c`, `scanner.c`.**
  The whole stage is `native.c` + `test_stdlib.c` + the
  Makefile `-lm` addition. clox is unchanged outside of these
  three files.

## Verification

```
$ cd 05-vm/clox && make clean && make
[no warnings under -Wall -Wextra -std=c99 -pedantic]

$ make test
==> bin/test_clox
15 passed, 0 failed
==> bin/test_repl
5 passed, 0 failed
==> bin/test_stdlib
21 passed, 0 failed

$ make valgrind
[in use at exit: 0 bytes in 0 blocks, 0 errors, on all three binaries]
```

## Bug caught during implementation (fourth time this project)

My `test_number_sqrt` included `print number_sqrt(-1);` in the
*success-case* test, expecting the runtime error to be a soft
failure. But **script mode exits 70 on runtime error**, so the
test got `exitCode=70` and failed.

**Fix:** split into two tests. `test_number_sqrt` (success
cases: positive and zero inputs) and `test_number_sqrt_negative`
(failure case, expects nonzero exit AND asserts the error
message contains "non-negative").

**Lesson:** in script-mode tests, runtime errors are *fatal to
the script*, not just to the line. The test must either:
1. Avoid the error case in a success-case test, OR
2. Split it out into its own test that expects the failure exit
   code.

This is the *fourth* test-side bug in this project. The pattern
is clear: my test instincts catch the *implementation* bugs but
underestimate the *script-mode-vs-REPL* distinction and the
*test-framework-lacks-X* issues. The fix is to ask, before each
test, "what assumptions am I making about how the script will
run?" and "what features does the test framework have that I
might be silently relying on (or not)?"

## The shape at four stages

Stages 7–10 are all in the same family: native function additions.
Total is now 19 native functions in `src/native.c` (~410 lines).

The pattern is stabilizing:
- **Tests first.** Always. Watch them fail, then make them pass.
- **Single-file diff.** `native.c` + `test_stdlib.c` (+ Makefile
  for the `-lm` addition in Stage 10).
- **Valgrind clean.** Every time. The `ALLOCATE`/`FREE_ARRAY`
  discipline is the same as for string natives.
- **Close-out doc on the same day** as the code. The retrospective
  asked for this discipline; I'm now in the habit.

The lesson is becoming a *rhythm*. That's the point of the
project: not the specific natives, but the loop of "test,
implement, verify, document, commit, push, PR, write close-out,
send Telegram, update wiki, pick next." Each iteration reinforces
the loop.

## What's next

After 4 stages of stdlib, the question is "when do I switch
families?" A few options:
1. **More string natives** (char_at, compare, repeat). Same
   family as Stages 8-9. Diminishing returns on the lesson curve.
2. **More number natives** (log, exp, sin, cos). Same family
   as Stage 10. Diminishing returns.
3. **Switch to a new family** entirely. Maybe `io_print` (write
   to stderr), `io_read_line` (read from stdin), `system_cmd` (run
   a shell command). Different lesson curve (host boundary, file
   descriptors, processes).
4. **Add arrays to clox.** Bigger change, unblocks `string_split`.
5. **Modules.** The big one.

My pick for Stage 11: **switch to the `io_*` family**. The
host-boundary lesson is one I haven't learned yet in this
project, and it's the most useful thing a stdlib can do
(reading from stdin is what makes a REPL useful for actual
work, not just demos). The natives are small (`io_print(s)`,
`io_read_line()`, `io_eprint(s)`, `io_exit(code)`) and the
lesson is about the C file-descriptor API and the
REPL's input handling.
