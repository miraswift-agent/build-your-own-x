# Stage 65.9 — DNS from scratch, second half: the transport and the cache (close-out)

**Date:** 2026-11-11 (CST evening) · **Branch:** `stage-65-sockets` · **Commit:** `ed0f26e`
**Component:** `06-sockets` (C) · **Predecessor:** 65.8 `20a0344` (the packets)
**Series:** 65.1 `56ce410`, 65.2 `301aad2`, 65.3 `c7a727e`, 65.4 `c2ec574`, 65.5 `9828192`, 65.6 `bc84304`, 65.7 `095d0f0`, 65.8 `20a0344`, 65.9 `ed0f26e`

---

## 1. The lesson

**A lookup is a UDP exchange, not a function call.**

Stage 65.8 built the packets. This stage sends them and remembers
them, and the whole shape of the stage is the difference between those
two words.

A function call has a shape: you call it, it returns a value, and the
only ways it can go wrong are the ones you wrote down in its signature.
A lookup has none of that. Between "I asked" and "I know" there is a
network, a clock, and another program, and **every assumption a
function call lets you make has to be earned here**:

- the answer may never arrive — **a timeout is a result, not a hang**;
- the answer may arrive from somebody else — a datagram from the wrong
  source is not our answer, however well-formed it is;
- the answer may be for a different query — the id has to match;
- the answer may be about a different question — the name and type have
  to match;
- the answer may be well-formed and still say no — NXDOMAIN, or
  NOERROR with nothing in it;
- and the answer may be **truncated**, which is not an error at all but
  a request to come back over TCP. This stage reports that and does
  not do it, and says so in the header.

The second half of the lesson is the cache. Once the answer exists, the
next lookup for the same name should not touch the network at all —
and that makes **time a first-class input**. A cache that ages against
the wall clock can be made to expire by an NTP step or by daylight
saving, and the failure is silent: entries that should be fresh look
stale, or worse, the other way round. So the clock here is **injected**,
not read from the library. The caller says what time it is. The tests
say it too, which is why every expiry test in this stage is
deterministic instead of a sleep.

## 2. What changed

New files `src/dns_resolver.c` and `src/dns_resolver.h`; `src/dns.h`
gained seven statuses for outcomes that are not "malformed message"
(`TIMEOUT`, `IO`, `ID_MISMATCH`, `QUESTION`, `SOURCE`, `NXDOMAIN`,
`RCODE`).

**`dns_resolve()`** — the exchange, in eleven numbered steps:

1. the cache, because a fresh entry is the whole point of having one;
2. build the query (65.8's builder);
3. open the UDP socket (lazily, once, and reused);
4. send — a datagram is all or nothing, so a short send is a failure
   and not a partial transfer to be resumed;
5. wait, with `poll()` and a timeout;
6. read the answer;
7. **whose answer is it?** — the source address and port have to be the
   server we asked;
8. the header — QR has to be set, the id has to be ours, TC means
   truncated;
9. the rcode — NXDOMAIN is NXDOMAIN, SERVFAIL is a refusal;
10. the question — the name and type have to be the ones we asked;
11. the answers — the first record that matches, cached under the name
    that was *asked* for (a CNAME chain still answers the question
    that was asked).

**`dns_cache_*()`** — a fixed table of 32 entries keyed by (name, type):

- names compare **case-insensitively** — "Example.COM" and
  "example.com" are the same name, and a cache that treats them as
  different keys serves the same answer twice;
- an entry that has expired is a miss **and is dropped**, because a
  cache that only forgets on a write keeps dead entries until the next
  write happens to need the room;
- a TTL of zero is stored as already-expired, which is exactly what
  "do not cache this" means;
- when the table is full, **expired entries go before live ones**, and
  if there are none, the entry that expires soonest is evicted;
- a hit reports the TTL that is **left**, not the TTL that was stored.

## 3. The oracle

Stage 65.8 checked the packets against captured wire bytes. This stage
has two oracles, and they check different things.

**The fake server** — a real socket and a real process. A child binds
a UDP socket on loopback and answers queries on it; the parent resolves
names against it. Nothing is mocked: the packets go out, the packets
come back, and the resolver has to be right about all of it. The server
can misbehave on purpose, and each way has its own test:

| Mode | What the server does | What the resolver must say |
|---|---|---|
| 1 | says nothing | `DNS_ERR_TIMEOUT` |
| 2 | answers with the wrong id | `DNS_ERR_ID_MISMATCH` |
| 3 | answers a different question | `DNS_ERR_QUESTION` |
| 4 | sets the TC bit | `DNS_ERR_TRUNCATED` |
| 5 | says NXDOMAIN | `DNS_ERR_NXDOMAIN` |
| 6 | says NOERROR with no answers | `DNS_ERR_NOTFOUND` |
| 7 | answers from another socket | `DNS_ERR_SOURCE` |

**The live check** — and this is the one that matters, because a fake
server can share a bug with the client that talks to it. If both sides
of my code got the byte order wrong in the same way, every test would
be green and the resolver would still be useless.

So the resolver was pointed at the actual system resolver
(`127.0.0.53:53`) and asked for real names:

```
example.com      status=ok  rcode=0 owner=example.com     ttl=300 -> 104.20.23.154
www.example.com  status=ok  rcode=0 owner=www.example.com ttl=296 -> 172.66.147.243
```

And `dig`, against the same resolver, at the same moment:

```
$ dig @127.0.0.53 example.com A +short
172.66.147.243
104.20.23.154
$ dig @127.0.0.53 www.example.com A +short
104.20.23.154
172.66.147.243
```

Both names have both addresses; the server rotates the order, and this
resolver returns the **first matching record**, which is why its order
differs from dig's on any given run. That is a deliberate simplification
and it is named: a resolver that returns *every* answer record is a
different API, and one address is enough to make the point.

So the chain is: my builder → `systemd-resolved` → the real DNS →
my parser → an address that `dig` independently confirms. The verifier
was verified against a program that was not written by me.

## 4. The tests

22 new tests:

- **the cache** — starts empty; finds what it stored; compares names
  case-insensitively; an expired entry is a miss *and* is dropped; a
  TTL of zero is never a hit; the key is the name **and** the type;
  storing the same key replaces rather than duplicates; a full table
  evicts the entry that expires soonest; a full table reclaims expired
  entries before live ones.
- **the exchange** — resolves a name over a real UDP socket; the query
  we send is a well-formed DNS query (checked from the *server's* side:
  QR clear, one question, the type we asked for); the second lookup of
  the same name does not touch the network; an expired cache entry is
  looked up again; and the seven misbehaviour modes above.
- **the edges** — a malformed server address is refused; an illegal
  name is refused *before anything is sent* (the query counter proves
  no packet left).

The component total is now **137/137**: 8 (65.1) + 8 (65.2) + 8 (65.3) +
13 (65.4) + 18 (65.5) + 21 (65.6) + 16 (65.7) + 23 (65.8) + 22 (65.9).

## 5. The findings

### 5.1 "Shared" was not shared, and the hang was the finding

The fake server's state — the port, the query counter, the behaviour
mode — was a struct **on the parent's stack**. The child inherited a
*copy*. So the parent's "stop" flag never reached the child, the child
kept answering queries forever, and the parent sat in `waitpid()`
waiting for a child that was never going to stop.

The first run hung, and the hang was the finding: **forked processes do
not share memory, and the word "shared" in a variable name is not a
memory model.** It is a `MAP_SHARED | MAP_ANONYMOUS` mapping now, and
the two processes really do see the same bytes.

It is worth naming the shape of this bug, because it is the same shape
as Stage 65.4's: *the failure is not where you are looking.* The code
that hung was not the code under test — it was the test harness's
cleanup path. The stage's own logic was fine.

### 5.2 Three tests failed for one reason, and it was not theirs

With the mapping fixed, three tests still failed:

- "the second lookup does not touch the network" — the server saw more
  than one query;
- "an expired cache entry is looked up again" — the server saw the
  wrong count;
- "an illegal name is refused before anything is sent" — a query went
  out anyway.

All three: the parent wakes the child up by sending it a datagram, and
the child counted that datagram **as a query** before checking the stop
flag. The counter now checks the stop flag first.

The generalisation: **a test that fails for a reason other than the one
it is testing will mislead you later** — the next person to read
"the server saw more than one query" will go looking for a caching bug
that does not exist. The failure message has to point at the cause.

### 5.3 The cache needed the clock more than the socket needed anything

This was not a bug but a design finding, and it changed the API. The
first draft of the cache asked the library what time it was, and every
expiry test became a `sleep()`. Making the clock a parameter turned
nine cache tests into instant, deterministic assertions — and it is
also the *correct* design: the monotonic choice is a real one, and a
wall-clock cache has a real failure mode (an NTP step silently
invalidates every entry).

The one thing this stage cannot make deterministic is the timeout. It
is a real wait, a real clock, and a real socket; the test asserts that
it returns `DNS_ERR_TIMEOUT` rather than hanging. That one is honest
about being a timing test.

## 6. Valgrind

**0 errors, 0 leaks** across all nine binaries and every forked child
(49 valgrind processes, exit status 0 — `--error-exitcode=1` means any
leak or invalid access would have failed the build). "All heap blocks
were freed -- no leaks are possible."

## 7. What was left on the table, deliberately

- **TCP fallback for a truncated answer.** The TC bit is detected and
  reported; the retry over TCP is not implemented. That is the next
  obvious piece of the same lesson.
- **Multiple answers.** This resolver returns the first matching
  record. A resolver that returns every answer record is a different
  API.
- **Multiple servers and the `resolv.conf` file.** One server, hard
  coded at init.
- **CNAME chasing.** The parser can read a CNAME; the resolver does
  not follow one — chasing is a loop in the client, and the loop is
  where the cache keys get complicated.
- **EDNS0, DNSSEC, AAAA.** The transport does not change the packets.
- **The 512-byte limit as an error.** A message over that size is
  truncated, not malformed.
- **Retries.** One query, one wait. A resolver that retries is a
  resolver with a policy.

## 8. Provenance

This stage shipped in the judgment cycle that began at 01:42Z, about
twenty minutes after the previous cycle shipped 65.8. It took roughly
the same shape as 65.8: a stage sized to the window, the commit-first
discipline (tests → valgrind → **commit** → close-out → wiki), and the
close-out written while the commit was already green.

The design doc's table is now complete: 65.8 the packets, 65.9 the
transport and the cache, and 65.10 the concurrency measurement.

## 9. Next

**Stage 65.10 — two concurrency models on one server, measured.**
`fork()` versus an event loop, on the same workload, with numbers:
connections per second, latency distribution, memory per connection,
and what happens to both when one client stops reading. The component
already has both models — 65.3's forking accept loop and 65.5's
`select()` multiplexer — so this stage is about putting them side by
side and letting the measurement decide, rather than an opinion about
which is better.

### What would change the pick

Tom can redirect the target in one message — the pick is mine, the aim
is his. Any of these moves 65.10 aside: a steer to close the sockets
arc after this and go back to the clox ladder; a steer toward the
browser's one genuinely open item (the active-formatting-elements list
and the `<a>` special case); a request to add TCP fallback for
truncated DNS answers, which is the same lesson as this stage and
would slot in as 65.9b; or a request to spend the time on the Vercel
publish gate instead of the ladder.

What would **not** change the pick is silence. The ladder is mine to
walk, and the sockets arc has exactly one stage left in it.
