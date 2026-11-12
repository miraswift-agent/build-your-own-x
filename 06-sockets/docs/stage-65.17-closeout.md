# Stage 65.17 — the server sends `Date:` — close-out

**Date:** 2026-11-12 · **Component:** `06-sockets` (C) · **Code:** this commit
**Branch:** `stage-65-sockets` · **Tests:** 8 new (component total **205**)

---

## 1. What shipped

RFC 9110 §6.6.1: **an origin server MUST generate a `Date` header field
in all 2xx, 3xx, 4xx and 5xx responses**, unless the server does not
have a clock. Ours has a clock, and ours sent no `Date` at all.

```c
/* The Date header's value for a time, in IMF-fixdate form:
 * "Sun, 06 Nov 1994 08:49:37 GMT" (RFC 9110 5.6.7). */
int http_date_format(time_t when, char *out, size_t out_cap);
```

Both response builders now take the date:

```c
int http_response_build(..., int keep_alive, const char *date);
int http_response_build_chunked(..., int keep_alive, const char *date);
```

so **every response this server sends carries one**: the `200`, the
`404`, both `400`s (the routeless request and the unparseable one),
and the chunked head.

### The date is a parameter, not something the builder looks up

The builder **does not read a clock**. The server that answers is the
one that knows when it answered, and it passes that down. This is the
same principle the DNS cache taught us in Stage 65.9 — *the clock is
injected, because a wall-clock cache fails silently when the clock
steps* — applied to a header instead of a cache entry. `NULL` is
refused (`EINVAL`): a response without a `Date` is not one this server
sends.

### The names are written by hand

`strftime`'s `%a` and `%b` are **locale-dependent**. A server running
under a non-English locale would emit a `Date` no cache can parse. So
the day and month names are literal tables, and the output is the same
in every environment.

## 2. The oracle: Node, and the live wire

**Node's captured response carries its own `Date`**, in the same format
(`tests/node_capture.h` — `Date: Thu, 12 Nov 2026 14:14:51 GMT`). The
test asserts that ours has the same *shape* as Node's: both
IMF-fixdate, both the same length.

The live check, two implementations side by side:

```
ours: HTTP/1.1 200 OK  Date: Thu, 12 Nov 2026 16:07:42 GMT  Content-Type: text/html  Content-Length: 3  Connection: keep-alive
node: HTTP/1.1 200 OK  Date: Thu, 12 Nov 2026 16:07:32 GMT  Connection: keep-alive  Keep-Alive: timeout=5  Content-Length: 3
```

Same format, same weekday, same clock — and Node, notably, sends **no
`Server:` header**, so there is no `Server:` gap to close either.

## 3. The measurements

| | result |
|---|---|
| new tests | **8/8 passed** |
| component total | **205** (was 197), 19 binaries |
| valgrind | **0 errors, 0 leaks** on all nineteen binaries, exit 0 |
| proof 1 — `Date` removed from the plain builder | **6 of 8 fail** |
| proof 2 — `Date` removed from the chunked builder | the chunked test fails (7/8) |

### The first attempt at the proof did not execute

The first discrimination proof removed the `"Date: %s\r\n"` line from
the format string but left the argument behind. The build **failed**
(`-Werror`), `make` never replaced the binary, and the **stale** binary
ran and reported **8/8 passed** — a comfortable lie. The second attempt
deleted `build/stage_65_17` first, checked the build's exit code, and
only then ran the test. This is the Stage 65.14a lesson arriving again:
**a proof that did not execute proves nothing.**

### A test that quietly stops testing its claim

Stage 65.6's overflow test used a 32-byte buffer. The `Date` header
made the head longer than 32 bytes, so the test kept passing — but it
was now failing on the *head*, not on the body it was written to
measure. It was strengthened rather than left green: the buffer is now
sized to hold the head **exactly**, so the body still does not fit.

### A shape that changed

The 404 response was **96-byte head + 14-byte body = 110 bytes**
(Stage 65.6's close-out). It is now **133 + 14 = 147**: the `Date`
header is 37 bytes. The old number in the old document is a historical
record; this is the current one.

## 4. The findings

**The clock is injected, not looked up.** The builder does not read a
clock; the server passes the time it answered. Same principle as the
DNS cache's injected clock, and the same reason: a component that
reaches for the wall clock itself is a component nobody can test with a
fixed input.

**The RFC's example is a test case.** "Sun, 06 Nov 1994 08:49:37 GMT"
is not decoration — it is a fixed input with a fixed output, and it
proves the weekday is right (6 November 1994 *was* a Sunday). The
formatter's own test uses it.

**A locale is a dependency, and a header is not the place for one.**
`strftime("%a")` gives the day in the process's locale. A server under
a non-English locale would send a `Date` no cache can parse — a bug
that only appears on the machine that happens to have the locale.

**Every response needs it, not just the happy one.** The RFC says 2xx,
3xx, 4xx and 5xx — so the `404`, the `400`s, and the chunked head all
carry a `Date`. The error paths are the ones a builder call forgets.

**A proof that did not execute proves nothing** — and it is worth
saying plainly: the first proof *looked* like a pass.

## 5. What this stage deliberately does *not* do

- **No `Server:` header** — and Node does not send one either (the
  capture shows it), so there is no gap to close.
- **No `Last-Modified`** — we have no documents on disk to have a
  modification time.
- **The client still ignores the `Date`.** It has been *reading* one
  since Stage 65.13 (Node's capture carries it), and ignoring it is
  correct for a client that does not cache.
- **The single-document server still accepts a `Host`-less HTTP/1.1
  request** (RFC 9112 §3.2 wants `400`) — the named follow-up from
  Stage 65.16, unchanged.
- **The server still ignores a request body** — see Next.

## 6. Next

**Stage 65.18 — the server reads the request body.** Today the server
answers a request and leaves its body in the socket; on a keep-alive
connection those bytes are parsed as the *next* request. The client
already has a body parameter in its request builder, and the server
already knows `Content-Length` when the request carries one — the
server just does not read it. The claim to test: a request with a body
is answered, and **the next request on the same connection is still
answered correctly**. The oracle is Node again: the same request, two
implementations, the same bytes on the wire.

### What would change the pick

Tom can redirect this in one message: a steer to the `main` rename (the
OPEN question in `open-questions-for-tom.md` — a naming change, not a
capability); a steer to the browser's **outer-loop iteration cap** (the
one documented difference from the spec — it needs a *decision about
deliberately diverging from html5lib* before it needs code, and that
decision is Tom's); a steer to the sockets component's other deferred
items (TLS, `SIGPIPE`, CNAME chasing, EDNS0, TCP fallback); a steer to
the smaller HTTP conformance gaps instead (**`HEAD`** — the headers of
the `GET` and no body, RFC 9110 §9.3.2 — or **`405`** for a method the
server does not implement, which is a smaller stage than the request
body); a steer back to the clox ladder; or a request to spend the time
on the Vercel publish gate instead. Silence does not change the pick.
