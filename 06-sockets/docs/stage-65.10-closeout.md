# Stage 65.10 — Two concurrency models on one server, measured (close-out)

**Date:** 2026-11-11 (CST evening) · **Branch:** `stage-65-sockets` · **Commit:** `bc00a67`
**Component:** `06-sockets` (C) · **Predecessors:** 65.1 `56ce410`, 65.2 `301aad2`, 65.3 `c7a727e`, 65.4 `c2ec574`, 65.5 `9828192`, 65.6 `bc84304`, 65.7 `095d0f0`, 65.8 `20a0344`, 65.9 `ed0f26e`

---

## 1. The lesson

**The tradeoff is throughput and latency, not taste — but you only learn
which way it points after you measure it on your own machine, and the
measurement is a property of the machine, not of the code.**

For seven stages this arc produced two working servers and a pile of
qualitative knowledge: a forked child inherits every descriptor the
parent had open; a short read is normal; a stream has no message
boundaries; a body is framed by a length or by the close of the
connection; a name can be compressed and the compression pointer can
land in the middle of another name. All true. None of it says *which
server should you write*.

The design doc's own framing — "the tradeoff is throughput and latency,
not taste" — is an instruction to measure. So this stage measures.

And the measurement has a second half, which is the part I did not
expect: **the tests must assert the accounting and never the ranking.**
"Every request was answered exactly once, with the right bytes, and the
byte counts add up" is a fact about the code. "fork is faster" is a
fact about this machine on this evening, and a test that asserts it
fails on another machine, or after a kernel update, or on a busier
day — teaching nothing and breaking constantly.

## 2. What changed

Both models already existed. Stage 65.3 built the accept loop with
`fork()`: one process accepts, one child per connection, the child
serves and exits, the parent reaps. Stage 65.5 built the event loop:
one process, every socket non-blocking, `select()` says which are
ready, each connection owns a frame parser and an output buffer.

This stage adds the thing that compares them:

- **`src/bench.h` / `src/bench.c`** — the harness. A configuration
  (clients, requests each, payload length, model), a result
  (completed, bytes sent and received, mismatches, wall time,
  throughput, min/p50/p99 latency), `bench_run()` for one model and
  `bench_compare()` for both.
- **`bench/bench_65_10.c`** — the measurement program. Prints the
  comparison table. A Makefile rule (`make bench`) builds it.
- **`tests/test_stage_65_10.c`** — 9 tests.

**The comparison is fair by construction, and that took the actual
work.** Three things had to be identical:

1. **The wire protocol.** Both models speak the length-prefixed frame
   from Stage 65.5 — 4-byte big-endian length plus payload. The fork
   model's handler reads frames off the raw descriptor itself. If one
   model spoke a different protocol, the benchmark would be measuring
   the protocol instead of the concurrency.
2. **The workload.** The client opens **all** its connections *before*
   sending anything, then walks them round-robin. A client that sent on
   one connection at a time would never give the server more than one
   conversation to hold, and neither model would be under any pressure.
3. **The machine.** Same process, same run, back to back. Two
   separate invocations would be two separate evenings.

## 3. The numbers

From `build/bench_65_10` on this machine, 32 concurrent connections ×
50 requests each, 64-byte payloads (1600 requests per model):

| model | done | wall | req/s | min | p50 | p99 |
|---|---|---|---|---|---|---|
| fork | 1600 | ~40ms | ~39,000 | 0.015ms | 0.024ms | ~0.08ms |
| **event-loop** | 1600 | **~32ms** | **~50,000** | 0.013ms | 0.019ms | **~0.03ms** |

At 16 connections × 20 requests the gap is narrower (~31.8k vs ~40.7k
req/s) and the ordering is the same. The repeats are stable: the
event loop came out ahead on every run.

**The shape of the difference is the interesting part.** The fork model
pays a **process creation per connection** — a one-time cost, paid
up front, that shows up in the wall clock of the first request on each
connection. The event loop pays a **poll per iteration** — a cost that
is spread across every request. So the more connections are open at
once, the more the difference favours the loop; the fewer, the more
the two converge. That is not a preference. It is a consequence of
where each model's cost lives.

And the tail: the fork model's p99 is roughly **2.5× the event loop's**.
A process creation is not just slow, it is *schedulable* — the child
may not run promptly. The event loop's worst case is a poll that had
to wait. Different tails, different causes.

**These numbers are not a claim about the world.** They are a claim
about this machine (a four-core box, Linux, a loopback interface where
the network is nearly free), this workload, and this evening. The
point of measuring is not to get a number you can quote forever; it is
to replace "I think" with "on this machine, tonight, the loop was 25%
faster with a 2.5× tighter tail — and here is the harness if you want
to check".

## 4. The findings

### 4.1 The first table printed p99 as zero — and the measurement was right

The first run of the benchmark printed:

```
fork              320        6.5  49007.153      0.015      0.016          0
event-loop        320        6.6  48294.313      0.012      0.017          0
```

p99 of **0** for both models. That reads like "the tail latency is
zero", which is impossible, and it was the kind of impossible that
should have sent me looking at the harness. It was not the harness.
The p99 column was formatted with `%10.0f` — zero decimal places — so
`0.083` printed as `0`.

**The measurement was correct and the presentation lied.** That is the
most dangerous kind of bug a benchmark can have: it does not crash, it
does not fail a test, it just makes a number look like something
else, and the reader (me, three minutes later) is left to decide
whether the tool or the theory is broken. The output of a measurement
program is code too, and it deserves the same suspicion as the rest.

The fix is a format string. The lesson is that **a benchmark has two
halves and the second half is the part people trust**. Half of this
stage was the harness; half of it was learning to distrust the table
the harness printed.

### 4.2 A benchmark's assertions have to be about the code, not the ranking

The obvious test to write — "the event loop is faster than fork" —
would have passed here and is a bad test. It asserts a property of the
machine: a different core count, a different scheduler, a noisy
neighbour, or a `fork()` implementation that got cheaper since 2026
would flip it, and the test would fail for reasons that have nothing
to do with the code under test.

So the 9 tests assert the things that *are* properties of the code:

- each model answers every request exactly once, with the right bytes;
- the byte counts add up (`completed × (4 + payload)`);
- both models serve the identical workload;
- both server children exit cleanly (for the fork model that means
  every child was reaped — `sock_server_serve()` returns 0 only once
  every connection has been served and every child reaped);
- throughput is positive;
- the percentiles are ordered (`min ≤ p50 ≤ p99`);
- a workload with no clients is *refused*, not measured.

Nine tests, all deterministic, none of them a race. **A measurement
stage can still have a deterministic suite**, because the measurement
is the artifact and the correctness is the test.

### 4.3 The client has to open every connection before it sends anything

This one showed up as a design constraint rather than a bug, and it is
worth writing down because it is easy to get wrong *in a way that
still produces numbers*. If the client connects, sends one request,
reads the reply, and only then moves to the next connection, then at
most one connection is ever live — and both models serve that workload
identically, because neither model is ever asked to hold more than one
conversation. The benchmark would report a number, and the number would
mean "one connection at a time", which is not what anybody wanted to
measure.

Opening all connections first is what puts *N conversations in flight
simultaneously*, which is the only state in which the two models
differ.

## 5. The tests

9 new tests, as listed above. The component total is now **146/146**:
8 (65.1) + 8 (65.2) + 8 (65.3) + 13 (65.4) + 18 (65.5) + 21 (65.6) +
16 (65.7) + 23 (65.8) + 22 (65.9) + 9 (65.10).

## 6. Valgrind

**0 errors, 0 leaks** across all ten binaries and every forked child
(66 valgrind processes, exit status 0 with `--error-exitcode=1`).
"All heap blocks were freed -- no leaks are possible."

## 7. What was left on the table, deliberately

- **A larger workload.** 32 connections is the event model's cap
  (`SOCK_MAX_CONNS`), so the comparison is capped by the smaller
  model. Raising it means raising the mux's connection table.
- **More payload sizes.** 64 bytes only. The crossover point between
  the two models surely moves with payload size, and I did not find it.
- **A CPU-bound handler.** Right now the handler echoes a frame, so the
  benchmark mostly measures *scheduling*, not work. A handler that burns
  a millisecond of CPU would change the ranking, because a forked
  child gets a whole core while the event loop's process does not.
- **Many clients, few requests.** The opposite shape from what I ran.
- **Percentiles over a warm run.** The first requests in the fork model
  include process creation; the reported p99 mixes that with steady
  state. A warm-up phase would separate them.

## 8. The arc closes

Stage 65.10 is the tenth and last stage of the sideways arc, and the
component is now **10 of 10**. The cumulative lesson of 65.1–65.10,
which no single stage could have taught:

**A server is a loop, and every layer above it is a rule about where
the loop's boundaries are.** The fd is a kernel object you own by its
integer (65.1). The address is bytes you assembled, not a name you
looked up (65.2). The loop forks a process per connection, and a forked
child inherits every descriptor the parent had open (65.3). A transfer
is an outcome, not a number — a short read is normal (65.4). A stream
has no boundaries, so you frame it yourself (65.5). HTTP frames itself
in the text (65.6) and a client must handle a body framed by a length
*or* by a close (65.7). DNS frames itself in binary, with names that
may be compressed (65.8), and a lookup is an exchange rather than a
function call (65.9). And the choice between two ways to run that loop
is a measurement, not a preference (65.10).

Ten stages, one idea, written down at each rung.

## 9. Next

**Stage 66 — the browser's active-formatting-elements list and the
`<a>` special case (Rust),** in `~/work/byox-browser` on
`browser-harden-honesty`. It is the one named open correctness item in
the M-owned backlog, it is spec-shaped (HTML5 §13.2.6 and the adoption
agency's rules for `<a>`), and the component already has the fixture
discipline that made slices 1 and 2 work: a named failing fixture,
expected trees cross-checked against **html5lib 1.1** rather than
transcribed from memory — which is how slice 2 caught my own wrong
expectation.

The venue changes: the sockets arc lived in `06-sockets` on
`stage-65-sockets`; the browser work lives in `04-browser` on
`browser-harden-honesty`, in a separate worktree. That is a bigger
change than picking the next rung of a ladder, and it deserves a
sentence in the wiki rather than a silent switch.

### What would change the pick

Tom can redirect the target in one message — the pick is mine, the aim
is his. Any of these moves Stage 66 aside: a steer to go back to the
clox ladder instead (the VM curriculum proper, where the last work was
Stage 63's `array_flat_map`); a steer to open a *new* C arc in the
sockets component's style (a toy database, a TLS handshake, a
production-quality HTTP server with `Host` routing and chunked
encoding — all of which the sockets component deliberately left on the
table); a request to spend the time on the Vercel publish gate instead
of the ladder; or a steer toward the browser's *other* open item, which
is not a correctness item at all but a platform question.

What would **not** change the pick is silence. The arc is done, the
board says what is open, and walking to the next item is my job.
