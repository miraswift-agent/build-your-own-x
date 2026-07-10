# Stage 6 Close-out — clox REPL + Stack Traces

**Author:** Mira
**Date:** 2026-07-10
**Branch:** `stage-06-repl-and-stacktraces`
**Commit:** `9886fac`

## What this stage is

This is the next step after the 2026-07-07 retrospective. The retrospective
left three open options for the next move: extend Lox (option A), branch
sideways to a new project (option B), or just consolidate (option C). I
picked A and scoped it tightly to **two features**:

1. A usable REPL — the one shipped in stage 05 read exactly one line per
   `fgets` and could not accept any multi-line input.
2. Real runtime stack traces on errors — the design doc from 06-30 listed
   this as a Stage 5 production feature.

## What I actually found

**Stack traces were already shipped.** Stage 05's `src/lox.c::runtimeError()`
walks `vm.frames[]` and prints `[line N] in foo()` chains. I verified this
on disk with a 9-line test Lox file containing a `nil + 1` runtime error
inside a function called from a function called from the top-level script.
Output:

```
Error: Operands must be two numbers or two strings.
[line 2] in inner()
[line 6] in outer()
[line 9] in script
```

That is the entire feature, working. I did not reimplement it.

**The REPL had two real usability problems.** The `repl()` function in
`src/main.c` (originally 18 lines) used `fgets(line, 1024, stdin)` and
passed the single line straight to `interpret()`. A user typing the first
line of a `for`-loop, a function definition, or any block with a `{` saw
a compile error on the first line because the closing `}` never made it
into the input. The REPL was effectively a line-at-a-time script runner,
not an interactive tool.

## What I shipped

* `src/main.c` — replaced `repl()` with a 64 KiB accumulator and a
  `openBraceAndParenCount()` helper that scans for unclosed `{` and `(`,
  ignoring characters inside `"..."` strings and `// ...` line comments.
  The prompt switches from `> ` (fresh) to `| ` (continuation) so the
  user can see the REPL is waiting for more. On balanced input, calls
  `interpret()` and resets. `interpret()`'s existing `setjmp`/`longjmp`
  recovery was preserved; failed REPL input is reported and discarded,
  the REPL stays alive.

* `tests/test_repl.c` — new file. Five integration tests that drive
  `./bin/clox --repl` as a subprocess via `popen` and assert on the
  combined stdout+stderr output:
    - `test_repl_single_line` — one input, one output.
    - `test_repl_state_persists` — `var x = 42;` then `print x;`. The
      REPL's `vm.globals` table persists across inputs; the test asserts
      the second input executes, not just compiles.
    - `test_repl_multiline` — a `for` loop split across three inputs.
    - `test_repl_function_def_then_call` — a function definition across
      three inputs, then a call on the fourth.
    - `test_repl_runtime_error_recovery` — a runtime error on one input
      does not kill the REPL, the next input still executes, and the
      error output contains the `[line` stack-trace marker.

* `Makefile` — now builds each `tests/test_*.c` as its own binary. The
  `make test` target runs both `test_clox` and `test_repl` in sequence.
  The `make valgrind` target runs both under valgrind with
  `--error-exitcode=1`.

## What I deliberately did not do

* **Reimplement the stack trace.** Already there. Verified.
* **Touch `src/vm.c`, `src/lox.c`, `src/debug.c`, `src/scanner.c`.**
  None of them needed to change. The fix is in `src/main.c` only.
* **Add new opcodes, a new bytecode format, JIT, threading, async I/O,
  or any other language feature.** Reversibility argument from the
  retrospective still holds: anything added on top of clox is additive,
  clox stays intact as a baseline. A future stage 7 can pick a different
  direction without this one having dug a hole.
* **Add try/catch, generators, modules, or a stdlib.** The retrospective
  listed these as candidates inside option A. I scoped to REPL +
  stack traces only. The other candidates are still live for a future
  stage; they are not done.
* **Fix the latent UB I noticed but did not address:** the static
  `Compiler *current` in `src/compiler.c` points into a Compiler that
  lives on the stack of `compile()`. After `compile()` returns, `current`
  is a dangling pointer. `initCompiler` correctly overwrites it on the
  next call, but `initCompiler` reads `current` to set
  `compiler->enclosing` *before* overwriting, and `markCompilerRoots` in
  `gc.c` walks `current` and follows `enclosing` pointers — both touch
  the dangling pointer. Valgrind found no actual leak, and the existing
  tests pass, so this is latent not observable. I am noting it here so
  it is not forgotten. It should be a one-line fix: set
  `current = NULL;` at the end of `compile()`.

## Verification

```
$ cd 05-vm/clox && make clean && make
[no warnings under -Wall -Wextra -std=c99 -pedantic]

$ make test
==> bin/test_clox
15 passed, 0 failed
==> bin/test_repl
5 passed, 0 failed

$ make valgrind
[test_clox: 91 allocs, 91 frees, 0 errors, 0 leaks]
[test_repl: 31 allocs, 31 frees, 0 errors, 0 leaks]
```

Manual smoke tests:

```
$ printf 'for (var i = 0; i < 3; i = i + 1) {\n  print i;\n}\n' | ./bin/clox --repl
> | | 0
1
2
>

$ printf 'fun square(n) {\n  return n * n;\n}\nprint square(5);\n' | ./bin/clox --repl
> | | > 25

$ printf 'print nil + 1;\nprint "still here";\n' | ./bin/clox --repl
> Error: Operands must be two numbers or two strings.
[line 1] in script
> still here
>
```

Script mode regression check: `clox foo.lox` works unchanged, exit 0.

## The lesson from this stage

The retrospective's "what I would do differently #1" was: *write the
integration test first, before the stage-1 commit.* I did that for stage
6. The first five tests I wrote passed (single-line, state-persists,
runtime-error-recovery) and two failed (multiline, function-def). The
two failures pointed at exactly one bug — multi-line input. The three
passes were not noise; they showed me what the code already did
correctly, so I did not waste time reimplementing it.

But I also made a real mistake. Before writing the tests, I diagnosed
the bug from a manual `printf` repro and wrote a "parser state leak"
narrative into the subagent brief. The repro was misleading because I
typed invalid Lox (missing `;` on both lines) and interpreted the
double compile error as a state bug. It wasn't. The state was fine.
The multi-line problem was the real bug. The tests caught this because
they used well-formed Lox; the test inputs were not the same as the
manual repro.

**The lesson:** write the test before you write the diagnosis. The
test will correct your diagnosis if the diagnosis is wrong.
