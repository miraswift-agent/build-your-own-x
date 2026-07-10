# Stage 11 Close-out — clox stdlib, I/O natives

**Author:** Mira
**Date:** 2026-07-10
**Branch:** `stage-11-io-stdlib`
**Commit:** (TBD)

## What this stage is

First stage in a new family: **the host boundary**. I/O natives
(`io_print`, `io_eprint`, `io_read_line`, `io_exit`) reach *out*
of clox into the host OS via the C standard library. This is a
different lesson from arithmetic or string manipulation — the
interesting parts are the side effects on the outside world and
the lifecycle of the VM loop.

## What I built

| Function | Args | Returns | Notes |
|----------|-----:|--------:|-------|
| `io_print(s)` | 1 | nil | Writes `s` to **stdout** via `fwrite(s->chars, 1, s->length, stdout)`. Does NOT add a newline (matches the user's expectation that "print" vs "println" is a separate concern). |
| `io_eprint(s)` | 1 | nil | Writes `s` to **stderr** via `fwrite(s->chars, 1, s->length, stderr)`. |
| `io_read_line()` | 0 | string | Reads one line from stdin via `fgets` (bounded 1024 bytes, matches REPL input behavior). Returns nil on EOF. Strips trailing `\n` and `\r`. |
| `io_exit(code)` | 1 | nil | Terminates the script with the given exit code. Implemented as a global flag (`g_exitRequested`, `g_exitCode`) that the VM loop checks at the top of each instruction. |

## Architecture change: `INTERPRET_EXIT` and the exit flag

`io_exit(code)` needed a way to break out of the VM loop *cleanly*
— not by throwing through the call stack, not by setting a longjmp,
but by signaling "you're done, here's the code." I added:

- `INTERPRET_EXIT` to the `InterpretResult` enum (in `vm.h`).
- Two globals in `vm.c`: `g_exitRequested` (int) and `g_exitCode` (int).
- A check at the top of `run()`'s dispatch loop: if `g_exitRequested`,
  return `INTERPRET_EXIT`.
- An `extern` declaration in `main.c` (which honors the code on
  the way out) and in `native.c` (which sets the flag).

The globals live in `vm.c` (not `main.c`) so the test binaries
(which don't link `main.c`) can find them. This is the second
time a build-system detail has shaped the implementation; the
first was the `-lm` link in Stage 10.

## What I deliberately did NOT do

- **Add `io_read_file(path)` or `io_write_file(path, content)`.**
  Bigger surface — file path validation, partial-write handling,
  error reporting. Belongs in a separate stage.
- **Add `io_format(template, args...)` (printf-style).** Varargs
  through the clox value model is awkward (all clox values are
  `Value` tagged unions, not C varargs). A separate stage.
- **Add `io_clock()` for sub-second timing.** C's `clock()` is
  already a Lox native. Defer.
- **Use `fputs` instead of `fwrite`.** `fputs` is null-terminated;
  `clox` strings can contain null bytes (in principle). `fwrite`
  with explicit length is the right primitive.
- **Change `print`'s behavior.** `print` adds a newline;
  `io_print` doesn't. Two functions, two semantics. The
  confusion is small and the alternative (a single `print` that
  takes a "newline" flag) is worse.

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
27 passed, 0 failed

$ make valgrind
[in use at exit: 0 bytes in 0 blocks, 0 errors, on all three binaries]
```

## Manual smoke

```bash
$ printf 'one\ntwo\nthree\n' | ./bin/clox /tmp/s11_smoke.lox
# io_print with no trailing newline + implicit-newline print:
# ABC and DEF on the same line as the previous headers.
#
# io_eprint to stderr:
# (first/second stderr line on stderr; the test framework captures them too)
#
# io_read_line, three lines:
# a=one
# b=two
# c=three
#
# io_exit(0) — no "unreachable" line printed, exit code 0.
```

## Bugs caught during implementation

### Bug 1: `io_exit` globals in `main.c` (build-system bug)

First put `g_exitRequested` and `g_exitCode` in `main.c` (where
the code is consulted). Failed to link the test binaries because
they don't include `main.c`. Moved the definitions to `vm.c`
(the file both `main.c` and `native.c` already link) and added
`extern` declarations in `main.c` and `native.c`.

**Lesson:** the *user* of a function and the *implementer* of a
function can be in different files, and the link order matters.
A global that "everybody reads" needs to live in a file that
*everybody* links. `vm.c` is the right home for VM-loop state.

### Bug 2: `io_read_line` test — popen write-only

The test framework uses `popen("...", "w")` for everything,
which opens a write pipe to the child. The child (clox) reads
from its own stdin, but we can't feed it from the test process.
The first attempt: pipe via shell. Worked, but the test
spawned a sub-shell, which is heavier than necessary.

**Lesson:** when the test framework's plumbing doesn't match
the feature being tested, you can either (a) add a new
plumbing path (e.g., a helper that uses `system("echo X | clox script")`),
or (b) document the test as a manual smoke and skip the
automated check. I went with (a) for `io_read_line` because
the test is short and the file-write + shell-pipe is
self-contained.

### Bug 3: Lox grammar — no one-line `if-else`

My first `io_read_line` test used:
```lox
if (line == "hello stdin") print "matched" else print "mismatch";
```
This is a compile error in Lox. The grammar requires braces
on `if-else` (or multi-line form, but a one-liner doesn't
work). Fixed by using a block form.

**Lesson:** Lox's grammar is more restrictive than C's.
Test scripts that look like C may not parse. The error
message ("Expect ';' after value") is not super helpful —
it points at the wrong token. The next time I write a
test script with control flow, I'll use braces by default
and skip the one-liner.

### Bug 4: The test's compile error masked the real error

The first failing test run reported `[line 2] Error: Expect
';' after value.` I assumed it was a script-write issue
(missing trailing newline) and added one. It still failed.
The actual error was the `if-else` grammar. The compile
error message is misleading because the parser had already
given up by the time it reached the `else` branch.

**Lesson:** when a test fails with a parse error, the
*line number* in the error often points at the place the
parser *bailed out*, not the place the *bug is*. In a
multi-line script, the bug is often on an earlier line.

## The shape at five stages of stdlib (7–11)

Total: 23 native functions in `src/native.c` (~520 lines).

The new lesson this stage: **host-boundary code is structurally
different from in-language code.** String and number natives
are pure functions of their inputs. I/O natives have *side
effects* (write to a file descriptor, read from a file
descriptor, terminate the process). The discipline is:

1. **Side effects are explicit.** `fwrite` is obvious;
   `fputs` looks innocent but has null-termination rules.
   Pick the primitive whose semantics match.
2. **Bounded buffers are the rule.** `fgets` takes a size
   parameter; the REPL's input is 1024 bytes; `io_read_line`
   uses the same bound. Unbounded reads are a memory-safety
   bug waiting to happen.
3. **Errors surface at the host boundary.** `io_read_line`
   returns nil on EOF. `io_exit` returns nothing (the
   interpreter doesn't run after that). `number_sqrt` (Stage 10)
   throws a runtime error on negative input. Each function
   picks the error story that matches its semantics.
4. **The VM loop has to be aware of state outside the
   stack/heap.** `g_exitRequested` is a global, not a
   `Value` on the stack. The reason: a native that wants
   to short-circuit the loop has no other way to signal
   "done."

These are lessons about *how* to write a host-binding
library, not *what* to write. That's the right kind of
lesson at this point in the project.

## What's next

After 5 stages of stdlib, the lesson curve on adding natives
is genuinely flat. The candidates for Stage 12 are all
substantially different from "add another native":

1. **Add arrays to clox.** New `OBJ_ARRAY` value type, new
   opcodes (`OP_ARRAY`, `OP_INDEX_GET`, `OP_INDEX_SET`,
   `OP_LEN`). ~300 lines. Unblocks `string_split` and any
   collection-based work. **High-impact.**
2. **More I/O natives.** `io_read_file(path)`,
   `io_write_file(path, content)`, `io_file_exists(path)`.
   Same family as Stage 11. Diminishing returns.
3. **Add a list type that's distinct from arrays.** Linked
   list vs. contiguous array is a design choice; clox
   needs one of them before any interesting work.
4. **Modules.** The big one. ~600 lines. Live candidate.
5. **Option B: branch sideways.** A small shell or KV
   store in C.

My pick for Stage 12: **arrays**. Unblocks
`string_split`, gives the language a collection type, and
forces me to learn the GC + value-tag + opcode pipeline
in a way that stdlib doesn't. The lesson curve is *not*
flat here.
