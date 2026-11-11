# Stage 65.1 — the fd lifecycle (close-out)

**Date:** 2026-11-11
**Branch:** `stage-65-sockets` (byox) — design doc `06-sockets/docs/stage-65-sockets-design.md`
**Status:** COMPLETE — 8/8 tests pass, valgrind clean (0 errors, 0 leaks)

---

## 1. The lesson

**A connection is a kernel object; the process owns only an integer.**

Everything else in this arc is a consequence of that one fact, so the first stage is
deliberately thin. It is the vocabulary the later stages use: a descriptor is a
number, the kernel owns what the number means, and closing the number is the only
way to say goodbye.

The lesson that has to be learned here is not "calling `socket()` works." It is
**who owns what** — because every bug later in this arc comes from forgetting it.
A leaked descriptor is not a leak of memory; it is a leak of a *connection* that
the process cannot see, name, or close. A descriptor used after close is not a
stale integer; it is a handle to something the kernel has already reclaimed.

## 2. What was built

`06-sockets/src/sockets.{c,h}` — the smallest honest wrapper over the kernel
interface, plus `tests/test_stage_65_1.c` and a `Makefile` matching the house
convention (`-Wall -Wextra -Werror -std=c11`, `make test`, `make valgrind`).

| Function | What it does |
|---|---|
| `sock_listen_loopback` | socket → setsockopt(SO_REUSEADDR) → bind → listen → **getsockname** to resolve the port the kernel actually chose |
| `sock_accept` | accept one pending connection |
| `sock_connect_loopback` | socket → connect |
| `sock_read_once` / `sock_write_once` | one read() / one write() |
| `sock_close` | close() |

Two decisions worth naming:

- **The port is read back, not assumed.** Binding to port 0 asks the kernel to
  choose; the code then asks the kernel which one it chose (`getsockname`). A
  port that was chosen but never verified is a bug waiting for a test to
  accidentally pass.
- **`SO_REUSEADDR` is set up front.** Without it, a port still in TIME_WAIT refuses
  a new bind — the kind of failure that shows up once a day, in the wrong test.

## 3. Red first

The tests were written before the implementation. `make test` at that point:

```
make: *** No rule to make target 'src/sockets.c', needed by 'build/test_stage_65_1'.  Stop.
```

For a greenfield component this is the weak form of the discipline — the red is
"the thing does not exist," not "the thing is wrong." So the suite was then
**mutation-tested** (see §5), which is the stronger check: does this suite catch a
broken implementation, or does it merely agree with a correct one?

## 4. The tests (8)

Every assertion is about descriptors — which numbers exist, which are live, which
the kernel hands back:

1. `socket_returns_a_valid_fd` — the number names a live descriptor (`F_GETFD` succeeds).
2. `bind_to_port_zero_resolves_a_real_port` — the kernel's choice comes back to us.
3. `accept_returns_a_distinct_fd` — a connection is its own kernel object: the
   accepted descriptor differs from both the listener's and the client's.
4. `echo_round_trip_preserves_bytes` — the bytes survive the round trip, both ways.
5. `closed_fd_is_refused_with_ebadf` — using a closed descriptor fails with EBADF.
6. `closed_fd_number_is_reusable` — closing releases the number; the next socket()
   gets it back. *This is the lesson as an assertion: we own an integer, not a resource.*
7. `listening_fd_is_not_a_connection` — reading from a listening descriptor fails.
8. `double_close_reports_ebadf` — closing twice is an error, not a shrug.

## 5. The mutation check (does this suite test anything?)

`src/sockets.c` was temporarily changed so that `sock_close` **leaked the
descriptor** instead of closing it. The suite went **5/8**:

```
"closed_fd_is_refused_with_ebadf"                   ✗ read on a closed fd did not fail with EBADF
"closed_fd_number_is_reusable"                      ✗ the released descriptor number was not handed back
"double_close_reports_ebadf"                        ✗ closing an already-closed fd did not fail with EBADF
```

Three independent tests caught one broken line. The mutation was reverted and the
suite returned to 8/8. This is the check I skipped on 04-browser slice 1 and should
not skip again: a suite that has never seen a broken implementation is a suite that
has not been tested.

## 6. Valgrind — the step that was N/A for every Rust slice

```
==269192== HEAP SUMMARY:
==269192==     in use at exit: 0 bytes in 0 blocks
==269192==     total heap usage: 1 allocs, 1 frees, 4,096 bytes allocated
==269192== All heap blocks were freed -- no leaks are possible
==269192== ERROR SUMMARY: 0 errors from 0 contexts (suppressed: 0 from 0)
```

This component is C, so the loop's valgrind step is no longer "N/A — no C touched."
It is ground truth, and it will be for every stage of this arc. That was one of the
reasons for choosing C over Rust for this component: Rust's std would have hidden
the byte order and the framing, which are the lessons.

## 7. Honest scope — what this stage deliberately does *not* do

- **The address is still a library call.** `inet_pton` + `htons` build the
  `sockaddr_in`. Stage 65.2 replaces them with hand-packed bytes.
- **The write is naive.** `sock_write_once` is a single `write()`. A short write is
  not an error, and pretending otherwise here would rob Stage 65.4 of its lesson.
  The header says so, and the code does what the header says.
- **No concurrency.** No `fork()`, no `select()`. Stage 65.3.
- **`accept` blocks.** There is no timeout and no non-blocking mode yet — 65.5.
- **`sock_read_once`/`sock_write_once` are thin wrappers.** They exist to give the
  later stages one seam to harden, not to hide the syscalls.

## 8. What would change this pick

- **Tom redirects the arc's target.** One message. The strongest alternative is a
  concurrency ladder; the design doc says so explicitly. The pick is mine, the aim
  is his.
- **A browser spec example fails.** The active-formatting-elements list and the
  `<a>` special case in 04-browser are still unimplemented; a failing spec example
  outranks a new component, because "find a case where we are wrong" is the method
  that has actually worked.
- **Tom's visible problems move.** The Vercel publish gate, the xAI token, the
  content backlog are his and they outrank my curriculum.
- **65.2–65.3 prove too easy.** If the next two stages turn out to be an hour of
  `man` pages rather than stages, the ladder gets reshaped before it goes further.

## 9. Next

**Stage 65.2 — addresses by hand.** Replace `inet_pton`/`htons` with a hand-packed
`sockaddr_in`: the family and port as raw bytes in network order, the address as
four bytes, and a `getsockname`/`getpeername` round trip that shows what the
kernel actually stored. The lesson: *an address is a byte string, not a name.*

Then 65.3 — the accept loop with `fork()` per connection.
