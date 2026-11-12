# Stage 65.3 — the accept loop with `fork()` (close-out)

**Date:** 2026-11-11 (CST evening) · **Branch:** `stage-65-sockets` · **Commit:** `c7a727e`
**Component:** `06-sockets` (C) · **Predecessors:** 65.1 `56ce410`, 65.2 `301aad2`

> **Provenance note.** This stage was *started* by a sibling judgment session on
> 2026-11-11 ~17:46 CST and **orphaned** when that session hit the gateway's
> 900-second judgment timeout at 17:57:35, leaving an uncommitted tree
> (`sockets.c` + `sockets.h` modified, `test_stage_65_3.c` untracked, 4 of 8
> tests failing). This pass adopted that work rather than re-implementing it.
> The finding about the timeout is in §9.

---

## 1. The lesson

**A server is a loop, and the loop is where the concurrency lives.**

Stage 65.1 made the descriptor visible (a connection is a kernel object; the
process owns only an integer). Stage 65.2 made the address visible (an address
is a byte string, not a name). This stage puts a *process* in the middle: one
process accepts, one child per connection, the child serves and exits, the
parent reaps.

The piece the earlier stages made visible — and the thing this stage is
actually about — is that **a forked child inherits every descriptor its parent
had open, including the listening socket.**

- If the **child** keeps its copy, the port never drains when the parent goes
  away. The socket stays open and the connection queue does not empty.
- If the **parent** closes its copy, `accept()` has nothing to accept on. The
  server is dead in a way that still *looks* alive.

Both mistakes are silent. Neither crashes. Both are caught here.

The second piece the stage makes visible: **a child that is still serving must
never hold up the next accept.** The parent reaps with `WNOHANG` inside the
loop and blocks only once, at the end, when there is nothing left to accept.
That is the whole difference between a server that serves three connections
one after another and a server that serves three connections *at the same
time*.

## 2. What changed

`src/sockets.h` — three new declarations:

```c
typedef int (*sock_conn_handler)(int conn_fd, int index, void *ctx);

typedef struct {
    sock_listener listener;   /* the listening socket the parent accepts on */
    int   connections;       /* connections accepted and forked */
    int   children_failed;   /* children whose handler failed */
    int   children_reaped;   /* children the parent has reaped */
    pid_t last_child;        /* pid of the most recently forked child */
} sock_server;

int sock_server_start(sock_server *s, uint16_t port, int backlog);
int sock_server_serve(sock_server *s, sock_conn_handler handler, void *ctx,
                      int max_connections);
```

`src/sockets.c` — the implementation, ~90 lines:

- `sock_server_start` — a listening socket on loopback; `port 0` asks the
  kernel for a free one, which comes back in `s->listener.port`.
- `sock_reap_children(s, blocking)` — a helper. `blocking == 0` uses
  `WNOHANG` (used *inside* the accept loop); `blocking == 1` waits (used once,
  at the end).
- `sock_server_serve` — the loop. Per connection: `accept` (retrying on
  `EINTR`), `fork`, and then the fork splits the responsibilities:

| | parent | child |
|---|---|---|
| the connection fd | close it — the child owns it now | keep it, serve on it, close it when done |
| the listening fd | **keep it** — it is the only accept path | **close it** — it does not need it, and keeping it stops the socket draining |
| exit | stay in the loop | `_exit(rc == 0 ? 0 : 1)` |

The handler's return value is the child's exit status: `0` for success,
non-zero marks the child as failed, and the parent's reaper counts it into
`children_failed`.

`tests/test_stage_65_3.c` — 8 new tests (20 KB), in the same shape as the 65.1
and 65.2 suites: a real socket on loopback, real processes, a pipe carrying
the server's and the children's observations back to the test process.

## 3. Red first

The suite was **red at 4/8** when this pass inherited it. The failures were
not in the implementation. They were three bugs in the *tests*, all one shape:

> **The test asserted something the harness never actually set up.**

1. **`ctx.fail_index` / `ctx.block_index` were assigned after `harness_start()`
   forked the server.** `handler_ctx` is copied across the `fork`, so any
   assignment made afterwards is invisible to the server — and to its children.
   - Test 8 (`a_failing_handler_does_not_stop_the_loop`) set
     `ctx.fail_index = 1` *after* the fork, so the handler that was supposed
     to fail never failed. The failure read "the failing handler still
     answered" — which is the test telling the truth about a handler that was
     never told to fail.
   - Test 7 (`the_parent_accepts_while_a_child_is_serving`) set
     `ctx.block_index = 0` after the fork, so the handler never blocked. The
     test **passed for the wrong reason**: it asserts that the second
     connection is served while the first is blocked, but nothing was ever
     blocked, so a strictly serial server would also have passed it. That one
     is the more dangerous bug of the two — it was green and it was lying.

   The fix: `fail_index` and `block_index` are now **parameters of
   `harness_start()`**, assigned before the fork. A test that wants a failing
   or blocking handler says so at the call site, where it is visible.

2. **Tests 2, 3 and 4 never opened a client connection.** A child is only
   forked *after* `accept()` returns, so with no client the parent blocked in
   `accept()` forever: test 2 and 3 timed out waiting for a child that could
   not exist ("the child never reported in"), and test 4's `harness_wait` timed
   out waiting for a server that was still waiting for work. The fix: each of
   the three now opens one connection and closes it straight away — the child
   then reads end-of-file, finishes and exits, which also lets the server
   finish.

3. The general form of (1) and (2): **a no-op is invisible to a behavioural
   test.** The same lesson Stage 65.2 paid for in blood (a same-bytes change
   produced by the library instead of by hand was invisible to every
   behavioural test). Here it showed up as a test asserting an arrangement the
   harness never made.

Final: **8/8 in 65.3, 24/24 across the component.**

## 4. The tests (8)

| # | test | what it pins |
|---|---|---|
| 1 | `a_forked_child_serves_the_connection` | the child serves the connection; the bytes survive the round trip |
| 2 | `the_child_runs_in_its_own_process` | the handler's pid ≠ the server's pid, and its ppid **is** the server's pid |
| 3 | `the_child_closes_its_copy_of_the_listener` | inside the child, the listening fd is closed (`fcntl` probe) |
| 4 | `the_parent_keeps_its_listener_open` | in the server process, the listening fd is still open afterwards |
| 5 | `the_server_serves_connections_in_a_loop` | three connections, three correct answers, `connections == 3` |
| 6 | `the_parent_reaps_every_child` | `no_zombies` and `children_reaped == 2` |
| 7 | `the_parent_accepts_while_a_child_is_serving` | the second connection is served **while the first child is blocked** — the concurrency, and the point of the stage |
| 8 | `a_failing_handler_does_not_stop_the_loop` | a failing handler does not stop the loop; the server notices (`children_failed == 1`) and still serves the third connection |

Test 7 is the one worth keeping: it is the only one that could distinguish a
concurrent server from a serial one, and (after the fix) it does.

## 5. Two mutations

Both were caught. Neither caught by accident, but they were caught for
different reasons, and the difference is the finding.

### Mutation A — the child does not close the listener

```c
-            sock_close(s->listener.listen_fd);
+            /* MUTANT: child keeps the listener */
```

→ `8 → 7`, exit 1, `"the_child_closes_its_copy_of_the_listener" ✗ the child
still held the listening socket open`. **Caught by the assertion written for
it.** This is the whole stage: the mistake the lesson is about, caught
directly.

### Mutation B — the parent closes the listener after the fork

→ `8 → 7`, exit 1, but **not** by `"the_parent_keeps_its_listener_open"`.
It was caught by `"the_child_closes_its_copy_of_the_listener"`, and the
mechanism is worth stating plainly: the parent's next `accept()` returns a
**new** file descriptor, and the kernel hands out the number the just-closed
listener used. The child's `fcntl(fd, F_GETFD)` probe then finds a *valid*
descriptor — not the listener, but a live socket — and concludes "I still hold
the listening socket open".

So the mutation was detected, but the message was wrong about why. Two
consequences, both honest:

- The probe `fcntl(fd, F_GETFD) != -1` is only meaningful when nothing else
  has taken that fd number. Inside the child, nothing else opens a descriptor,
  so the probe is sound there. It is the *parent's* mistake that corrupts it,
  by reusing the number.
- **The assertion written for mutation B did not fire.** If mutation B had
  happened in a variant where the parent's next `accept()` returned a
  *different* fd number, nothing would have caught it. That is a real gap in
  the suite, named here rather than left to be discovered later.

### What the two mutations together say

Stage 65.2's finding was that a *no-op* change is invisible to behavioural
tests and only a contract test can see it. Stage 65.3's finding is the
neighbour: a *behavioural* change can be caught by the **wrong assertion**,
and a green suite can be reporting a fact about descriptor numbering rather
than about the lesson. Both are ways a test suite talks about something other
than what it claims to measure.

## 6. Valgrind

`make valgrind` over all three binaries: **0 errors, 0 leaks** — every process
reports `All heap blocks were freed -- no leaks are possible`, including all
the forked children (the connection children `_exit()` without running atexit
handlers, and valgrind still checks them). `valgrind --error-exitcode=1`
means the run's exit status is the verdict: exit 0.

This is the step that is N/A for the Rust browser component and ground truth
here, and it is why the sideways arc is written in C.

## 7. Honest scope — what this stage deliberately does *not* do

- **Only loopback.** `sock_connect_loopback` / `sock_listen_loopback` are the
  only transport. No DNS (that is 65.8), no IPv6.
- **The write is still one naive `write()`.** A short write is silently
  truncated. That is 65.4 — *errors as protocol*.
- **`accept()` still blocks** when no connection is waiting, so the server
  cannot be shut down cleanly from the client side. That is 65.5.
- **No concurrency limit.** One child per connection, unbounded. A flood spawns
  a flood.
- **No `SO_REUSEADDR` on the child side**, no `SIGCHLD` handling — children
  are reaped with `waitpid`, not with a signal.

## 8. What would change this pick

The next stage is **65.4 — errors as protocol: short reads and writes**. It
would be the wrong pick if:

- Tom wants the target redirected (he can do that in one message; the whole
  sideways arc is his to aim, mine to walk).
- The fork model turns out to be the wrong teaching order — for instance if
  the *loop* should come before the *process* (a single-process server that
  handles connections one at a time, and only then `fork`s). In that case
  65.3 should be reordered behind a 65.3a.
- The fd-numbering fragility in §5 gets addressed first. That is a
  test-suite fix, not a stage, but it is the kind of thing that should not
  be carried silently into the next component.

## 9. Next

**Stage 65.4 — errors as protocol: short reads and writes.** The write is
currently a single naive `write()`, and a short write is a silent truncation.
The next stage makes the failure visible *as data* — the same way Stage 65.2
made the address visible — instead of as a return code nobody checks.

### The structural finding from this stage's provenance

This stage was orphaned by the **judgment timeout**. The sibling session that
started it was killed by the gateway's 900-second cap (raised from 5 min to
15 min on 2026-11-11, commit `d4ed354`) while it was mid-stage, leaving an
uncommitted tree that the next pass had to adopt. The 15-minute cap is
**still not enough** to ship a stage end-to-end: tests, valgrind, mutation
checks, close-out doc, wiki INDEX, `projects.md`, `TASKS.md` and the daily log
is a lot of writing for a 15-minute window, and the failure mode is not a
clean failure — it is a **dirty tree that the next session inherits with no
memory of the intent**. That is exactly what happened here.

Two candidate fixes, both Tom's call or mine:

1. **Raise the judgment timeout further** (30 min). Cheap, but a stalled
   session then stalls the heartbeat for 30 minutes.
2. **Give the loop a commit-per-step discipline** — commit the work *before*
   the close-out writing, so an interrupted stage leaves a green commit and a
   short list of what is left, rather than an uncommitted tree. This is the
   better answer, and it is also the "Is it committed?" check from
   [[working-loop]] step 5, applied earlier in the order.

Option 2 is what this pass did *by accident* — the code was committed at
`c7a727e` before the close-out doc was written — and it is why adopting the
orphan was possible at all.
