# Stage 65.15b — the client pipelines — close-out

**Date:** 2026-11-12 · **Component:** `06-sockets` (C) · **Code:** this commit
**Branch:** `stage-65-sockets` · **Tests:** 5 new (component total **191**)

---

## 1. What shipped

Stage 65.14b gave the client a **connection**: it owns a socket, so a
second request can be written on it after the first response has been
read. That is **one turn at a time**. Pipelining is the other shape —
two requests go out **before** either response comes back — so the
connection API now separates the two halves:

```c
int http_client_connection_write_request(http_client_connection *c, const char *method,
                                         const char *target, const char *body,
                                         size_t body_len);
int http_client_connection_read_response(http_client_connection *c, http_response *out);
```

`http_client_connection_request()` is now write-then-read, one turn at
a time, **exactly as before** — nothing that used it changes.

## 2. The oracle: on the wire, twice over

> both requests leave before the peer answers either, and the responses
> come back **in the order the requests were written**.

### (a) The requests leave together — proved by the peer

The peer in this test **refuses to answer until it has seen both
requests**. A client that waited for the first response before writing
the second would wait forever. So the claim is settled by the wire,
not by the client's opinion of itself.

And because a hang is a poor failure mode, the test carries an
**alarm**: the failure is a message and an exit code, not a hung test
binary.

**The proof that the test discriminates:** with the client changed to
one turn at a time, this test **times out**:

```
two requests go out before either response                FAIL: timed out
```

Restored: **5/5**.

### (b) The order — proved twice, and visible in the bytes

**Our own server.** Two routes, two different bodies: `/` answers with
the plain body, `/stream` with the chunked one. A swapped pair is
visible.

**Node.** A raw TCP client wrote two requests in **one write** to
Node's HTTP server, and the raw bytes were captured: **322 bytes**, two
responses of 161 bytes each. Node's bodies **name their path** —

```
body for /a
body for /b
```

— so the order is not a subtle difference between two similar bodies,
it is a different body. The capture lives beside the other one in
`tests/node_capture.h`.

## 3. The measurements

| | result |
|---|---|
| new tests | **5/5 passed** |
| component total | **191** (was 186) |
| valgrind | **0 errors, 0 leaks**, 17 binaries, exit 0 |
| discrimination proof | one-turn-at-a-time → the pipelining test **times out** |

## 4. The findings

**Pipelining is a different question from keep-alive.** Keep-alive
asks *may this connection carry another message?* Pipelining asks *may
the next message go out before the last one comes back?* The first is
about the connection's **life**, the second about its **ordering** — and
the second needs the API split that the first did not.

**The proof belongs to the peer, not to the client.** A peer that
refuses to answer until it has seen both requests turns "the client
pipelined" into something the wire can settle. A test that checks the
client's own bookkeeping proves only that the client agrees with
itself.

**A body that names its path makes the order visible.** "body for /a"
and "body for /b" turn an ordering claim into a string comparison.
Compare bodies that differ only in whitespace and the claim becomes a
judgment call.

**The harness can be the deadlock.** The peer counted request lines
*per call*, so its second wait asked for two **more** requests — and the
client, waiting for a response the peer would not send, waited too.
The alarm is what turned that into a failure with a message instead of
a hung suite. The same shape as last cycle's race: **the race was in
the harness, not in the claim** — but this time it was a deadlock, and
a deadlock is what an alarm is for.

**The client's Content-Length path already kept the leftovers** — the
`memmove` that Stage 65.14a added for the *chunked* path had a twin
here all along. So no parser change was needed: the gap was in the
*client's shape* (write, wait, write), not in its parser.

## 5. What this stage deliberately does *not* do

- **No `Host` routing** — the client sends `Host:` and the server
  ignores it.
- **No TLS**, no `Date:` header, no `SIGPIPE` handling — the deferred
  items on the board are unchanged.
- **No reconnect**: a peer that goes away ends the connection.
- **The server's own pipelining** was already proven by Stage 65.15a —
  this stage is the client half.

## 6. Next

**Stage 65.16 — the server routes by `Host`.** The header is already
on the wire (our client sends it, Node sends it) and our server
ignores it, so a virtual-host deployment cannot work. The oracle: two
requests with different `Host` headers get different bodies — and the
independent oracle is **curl**, which is not us:
`curl -H "Host: other.example" http://127.0.0.1:PORT/`.

### What would change the pick

Tom can redirect this in one message: a steer to the `main` rename (the
OPEN question in `open-questions-for-tom.md` — a naming change, not a
capability); a steer to the browser's **outer-loop iteration cap** (the
one documented difference from the spec — it needs a *decision about
deliberately diverging from html5lib* before it needs code, and that
decision is Tom's to make); a steer to the sockets component's other
deferred items (`Date:`, TLS, `SIGPIPE`, CNAME chasing, EDNS0, TCP
fallback); a steer back to the clox ladder; or a request to spend the
time on the Vercel publish gate instead. Silence does not change the
pick: the board names the sockets component's HTTP arc, and `Host`
routing is its next open seam.
