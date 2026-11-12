# Stage 65.4 — errors as protocol (close-out)

**Date:** 2026-11-11 (CST evening) · **Branch:** `stage-65-sockets` · **Commit:** `c2ec574`
**Component:** `06-sockets` (C) · **Predecessors:** 65.1 `56ce410`, 65.2 `301aad2`, 65.3 `c7a727e`

---

## 1. The lesson

**`read()` returning less than asked is normal, not an error.**

Stage 65.1 shipped `sock_read_once` / `sock_write_once` on purpose: one
syscall, a number back. The header said so outright — *"a short read or a
short write is NOT reported as an error here: it is stage 65.4's lesson."*
That was the stage leaving a hole it had named, and this stage fills it.

The hole was not cosmetic. The naive form hands back a number and leaves the
caller to notice that a number smaller than the one it asked for is neither
a success nor a failure. A caller that writes

```c
sock_write_once(fd, buf, len);   /* return value ignored */
```

has silently truncated its buffer and cannot tell. The bytes are gone and
nothing says so. The failure mode is not a crash — it is a message that
arrives short, or does not arrive at all, and a server that has no idea.

Stage 65.3 made the *process* visible (a child inherits the listener). This
stage makes the *outcome* visible: the transfer answers with what happened,
not with a number that has to be interpreted.

## 2. What changed

`src/sockets.h` — a status enum and a result struct:

```c
typedef enum {
    SOCK_IO_COMPLETE = 0,   /* everything asked for was transferred */
    SOCK_IO_PARTIAL,        /* some bytes moved, but not all -- NOT an error */
    SOCK_IO_EOF,            /* the peer closed: no more bytes will come */
    SOCK_IO_INTERRUPTED,    /* EINTR: a signal arrived; the call is retryable */
    SOCK_IO_WOULD_BLOCK,    /* EAGAIN: non-blocking socket, nothing to do now */
    SOCK_IO_ERROR           /* a real failure; err holds the errno value */
} sock_io_status;

typedef struct {
    sock_io_status status;
    size_t          transferred;  /* bytes actually moved */
    int             err;          /* errno value when status is SOCK_IO_ERROR */
} sock_io_result;
```

Five functions, and the naming is the point:

| Function | What it does |
|---|---|
| `sock_read_some(fd, buf, len, &r)` | one `read()`, outcome as data |
| `sock_write_some(fd, buf, len, &r)` | one `write()`, outcome as data |
| `sock_read_exact(fd, buf, len, &r)` | loop until the whole request is satisfied |
| `sock_write_exact(fd, buf, len, &r)` | loop until the whole request is satisfied |
| `sock_io_status_name(st)` | the name of a status, for messages |

The `*_exact` pair loops because a partial transfer is normal: the kernel
gave you what it had and you go back for the rest. The loop stops on
anything that trying again cannot fix — an error, a closed peer, a
non-blocking socket with nothing available — and the result says **how far
it got**. A `write_exact` that stops at `WOULD_BLOCK` after N bytes reports
N, so the caller knows exactly how much is already on the wire.

## 3. The protocol, and the one rule that makes it work

The six statuses are not variations of "error". They are six different
situations and the caller treats them differently:

| Status | The caller should |
|---|---|
| `COMPLETE` | carry on |
| `PARTIAL` | carry on, but know the tail is missing |
| `EOF` | stop reading; there will never be more |
| `INTERRUPTED` | retry — a signal arrived, not a failure |
| `WOULD_BLOCK` | come back later — not now, not never |
| `ERROR` | give up; `err` says why |

**The rule: `err` carries an errno only for `SOCK_IO_ERROR`.**

That rule is the stage. If `err` is filled for `EINTR` and `EAGAIN` as
well, then `err != 0` means *"something happened, sometimes harmless"* —
which is precisely the ambiguity the stage exists to remove. The status
already names those two cases; the errno field is for the case where the
errno is the only thing you know.

This was not a design I got right the first time; see §6.

`EOF` is the other distinction worth stating: **EOF is not a short read.**
`read()` returning 0 and `read()` returning fewer bytes than asked are both
"less than you wanted", and they mean opposite things — one is the end of
the stream, the other is the middle of it. The naive API cannot tell them
apart without a second call.

## 4. The tests

13 new tests, `tests/test_stage_65_4.c`. They check the *protocol*, not the
plumbing:

- a short read is `PARTIAL`, with the bytes that arrived
- a read of exactly the available bytes is `COMPLETE`
- a closed peer is `EOF`, not `PARTIAL`, and not an error
- `read_exact` assembles across two partial reads (the child writes 4
  bytes, waits 120ms, writes 4 more — one `read()` cannot get all eight, so
  a `COMPLETE` result proves the loop ran)
- a signal during a blocking read is `INTERRUPTED` and retryable — the
  handler is installed **without** `SA_RESTART`, and a child keeps signalling
  until the read is interrupted, so the test cannot lose a race
- a non-blocking socket with nothing available is `WOULD_BLOCK`, with
  `err == 0`
- a closed descriptor is `ERROR` with `err == EBADF` — the errno travels
  *with the result*, so the caller cannot lose it by calling something else
  first
- a write that fits is `COMPLETE`
- a write that cannot fit is not `COMPLETE` — it is `PARTIAL` or
  `WOULD_BLOCK`, and either way the caller knows the tail is missing
- `write_exact` on a full socket reports `WOULD_BLOCK` **and the number of
  bytes already written**
- `write_exact` on a socket with room is `COMPLETE`
- the six statuses have distinct names

The last test is the contrast that gives the stage its teeth:

**`naive_write_once_truncates_silently`** — the same non-blocking socket
with the same small send buffer, written twice. The old API returns a
number (`sock_write_once` → `naive`, less than the length asked for) that a
caller may ignore; the new API returns `PARTIAL`/`WOULD_BLOCK` with the
count. Both calls transfer the same bytes. Only one of them says what
happened.

### Hermetic

Socketpairs and loopback only — no network, no ports, no routing table. The
EINTR test uses `fork()` and `kill()`, both local.

## 5. Valgrind

**0 errors / 0 leaks** on all four binaries and every forked child
(`make valgrind`; 24 valgrind processes across the four suites, each child
is its own process). The component total is now **37/37 tests**: 8 (65.1) +
8 (65.2) + 8 (65.3) + 13 (65.4).

## 6. The finding: an impl bug that the protocol had to forbid

The first implementation of `sock_read_some` / `sock_write_some` filled the
`err` field for **every** failed syscall:

```c
sock_io_set(out, st, transferred, (n < 0) ? saved : 0);
```

Three tests failed on the first run — and all three failed the same way:

```
read_some_reports_interrupted      ✗ expected INTERRUPTED/0/0, got interrupted/0/4
read_some_reports_would_block      ✗ expected WOULD_BLOCK/0/0, got would-block/0/11
write_exact_reports_progress...    ✗ expected WOULD_BLOCK with 0<transferred<1048576, got would-block/8064
```

`4` is `EINTR`. `11` is `EAGAIN`. The *statuses* were right in every case —
the implementation was naming the situation correctly while simultaneously
reporting it as an error. A caller that scanned `if (r.err != 0)` for
failure would have treated a retryable signal and a harmless would-block as
failures, and would have given up on a socket that was merely busy.

The fix is one line at two sites, but the finding is the rule in §3:
`err` is set **only** for `SOCK_IO_ERROR`. The status names the rest.

This is the same shape as the Stage 65.2 finding, one level up. 65.2 found
that a *no-op* change is invisible to every behavioural test — only a
contract test that reads the source can see it. 65.4 finds that a
*harmless* value in an error field is invisible to every test that only
checks the status — the tests that check `err` are the ones that notice.

## 7. What was left on the table, deliberately

- **`SIGPIPE`.** A `write()` to a peer that has closed raises `SIGPIPE`,
  whose default action kills the process. This is the same family of
  problem — a failure that arrives as a *signal* rather than as data — and
  it is not in this stage. It wants `MSG_NOSIGNAL` or a `SIG_IGN`, and a
  test that closes the peer and writes. Named, not papered over.
- **`SO_REUSEADDR`** was in the design row for 65.4 but was already
  implemented in Stage 65.1 (`sock_listen_loopback` sets it, because a port
  in `TIME_WAIT` otherwise refuses a new bind). Nothing to do.
- **Non-blocking servers.** 65.4 makes `EAGAIN` legible, but nothing here
  *sets* `O_NONBLOCK` on a server socket — the non-blocking server is
  65.5, and this stage is the vocabulary it will need.
- **`EINTR` on `accept()`.** The accept path in 65.3 retries `EINTR`
  inline; it does not report it as data, because `accept()` has no bytes to
  report. Out of scope here.

## 8. Provenance: the commit-first discipline

Stage 65.3 was orphaned by the 900-second judgment timeout and left an
uncommitted tree that the next pass had to adopt blind. Its close-out
named two candidate fixes and recommended the second: **commit the code
before writing the close-out**, so an interrupted stage leaves a green
commit and a short list of what is left, rather than a dirty tree with no
memory of the intent.

This stage ran under that discipline. The code was committed at `c2ec574`
(with its tests and its valgrind clean) *before* this document was
written. If this session had been killed mid-write, the next pass would
have found a green commit and one file to write — not an orphan.

The discipline is now the loop's step 4 with the ordering moved:
**tests → valgrind → commit → close-out → wiki**, and the commit is the
point of no return, not the last step.

## 9. Next

**Stage 65.5 — non-blocking + `select()`/`poll()` multiplexing with
per-connection buffers.** The accept loop blocks, so the server cannot be
shut down cleanly from the client side, and one slow client holds up every
other connection. This stage makes the server multiplex: one process, many
connections, each with its own buffer, because a stream has no message
boundaries and you must frame it yourself.

Stage 65.4 is the vocabulary that stage needs: `WOULD_BLOCK` is not an
error, it is "not now", and a partial transfer is data.

### What would change the pick

Tom can redirect the target in one message — the pick is mine, the aim is
his. Concretely, any one of these moves 65.5 aside: a steer toward HTTP
(65.6) instead of multiplexing; a steer toward the browser's remaining
parser work (the active-formatting-elements list and the `<a>` special
case, the one genuinely open browser item); or a request to close out the
sockets arc at 65.4 and go back to the clox ladder. The one thing that
would *not* change the pick is silence — the ladder is mine to walk, and
65.5 is the next rung Tom has already seen described.
