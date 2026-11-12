# Stage 65.7 — HTTP/1.1 client from the wire (close-out)

**Date:** 2026-11-11 (CST evening) · **Branch:** `stage-65-sockets` · **Commit:** `095d0f0`
**Component:** `06-sockets` (C) · **Predecessors:** 65.1 `56ce410`, 65.2 `301aad2`, 65.3 `c7a727e`, 65.4 `c2ec574`, 65.5 `9828192`, 65.6 `bc84304`

---

## 1. The lesson

**The client has to know how the body ends, and there are two ways to know.**

Stage 65.6 built a server that frames every response with
`Content-Length`: it says how many bytes follow, so the reader knows
where the message stops. A client cannot assume that. A response with no
`Content-Length` is framed by the **close of the connection** — the body
is whatever arrived before the peer hung up. Both framings are legal
HTTP, and a client that handles only one of them has a bug that shows up
as a truncated page (a body silently cut short) or as a hang (waiting
for bytes that will never come).

So the client gets a third answer, alongside `NEED_MORE` and `ERROR`:
**`FINISH`**. When the peer closes, an EOF-framed response is complete.

And the mirror image of 65.6's rule: a response that *did* carry a
`Content-Length` and ended early is not a short response. It is a
**truncated** one, and the client has to say so — because "I got fewer
bytes than promised" and "that was all there was" are different facts,
and a client that cannot tell them apart will hand the user half a page.

The framing rule, whole:

| Response says | Body ends | Complete when |
|---|---|---|
| `Content-Length: N` | after N bytes | N bytes have arrived |
| no `Content-Length` | at the close | the peer closes |
| `Content-Length: N`, then close | never | **error** — truncated |

## 2. What changed

New files `src/http_client.h`, `src/http_client.c`, plus one addition to
`sockets.c`.

**The request builder** — `http_client_request_build()` assembles text:
`METHOD SP TARGET SP HTTP/1.1 CRLF`, a `Host` header, a `User-Agent`, a
`Connection` header, and the body with its `Content-Length`. It refuses
to write past the buffer it was given (`ENOBUFS`).

**The response parser** — `http_response_parser_next()` pulls one
response out of the buffer and leaves the leftover bytes for the next
one; `http_response_parser_finish()` completes an EOF-framed response
when the peer closes. The rules are the ones Stage 65.6 established, in
the opposite direction: the status line is `HTTP/1.1 SP code SP reason`
(and the reason may be empty), headers are `Name: value` with
case-insensitive lookup and trimmed values, the blank line `\r\n\r\n`
*starts* at the last header's own CRLF so the headers run to `blank + 2`
and the body starts at `blank + 4`, and with no headers at all the blank
is the status line's own CRLF plus an empty one — which puts the body
four bytes in.

**The round trip** — `http_client_get()` connects, writes the request,
reads the response, parses it. The address is a dotted quad: **a name
needs DNS, which is Stage 65.8.**

**One addition to `sockets.c`** — `sock_ipv4_from_dotted()`, because the
client has to turn what a human writes into what the kernel wants, and
this component counts octets by hand.

## 3. The tests

16 tests: 14 about the rules, 2 over a real socket.

- the request builder: the request line, `Host`, the blank line at the
  end; `Content-Length` for a body; and a buffer that is too small
- the status line: parsed; parsed with **no reason phrase**; a version
  that is not HTTP (rejected); a code that is not three digits (rejected)
- headers: case-insensitive lookup; a header with no colon (rejected); a
  `Content-Length` that is not a number (rejected)
- framing: a body that has not all arrived is `NEED_MORE`; **no
  `Content-Length` means the body ends at EOF**; a `Content-Length`
  response that ends early is an **error**, not a short body; a response
  with no headers at all parses
- over a real socket: a `GET /` that comes back with the body, and a
  `GET /missing` that comes back 404 — both against the Stage 65.6
  server, running in a child process on 127.0.0.1

The component total is now **92/92**: 8 + 8 + 8 + 13 + 18 + 21 + 16.

## 4. The findings

### A guard that greps the source can be tripped by the documentation of the rule it guards

Stage 65.2 removed every library address helper from the component and
enforced that with a test that scans `src/` for the banned names. My new
comment in `sockets.h` explained the rule by naming the helpers it
banned:

> *"this component does that by hand: no `inet_pton`, no `getaddrinfo`."*

The 65.2 suite failed on my prose. Not on my code — on the sentence
explaining that I did not use them.

The fix was to reword the comment. But the finding is bigger than the
fix: **a source-scanning guard cannot tell code from commentary**, so
the documentation of a rule has to avoid the rule's keywords, or the
guard has to strip comments before it greps. I put a note in the comment
saying so — a sentence about the sentence — which is at least honest
about the trap.

This is the second time this component has been bitten by the same
shape: **the value was right and the check was wrong.** Stage 65.6's
keep-alive test compared 25 bytes against a 24-byte literal, and the
server was right. Here the implementation was right and the *guard* was
right too — it fired exactly as written — but what it fired on was a
comment. The pattern worth keeping is not "checks are bad" but "a check
that scans text is only as good as what it can distinguish."

### The deliberate seam

The response parser in `http_client.c` is a near-copy of the request
parser in `http.c`: same boundaries, same rules, opposite direction. Two
copies for now. The risk is real and worth naming: **a client that
speaks a slightly different dialect of HTTP than the server it talks to
is exactly the bug that takes an afternoon to find.** The seam between
the two parsers is the thing to close later — one parser, two directions,
tested against both. Not this stage.

### What the client parser already supports but the client does not use

`http_response_parser_next()` consumes exactly one response and leaves
the leftover bytes in the buffer, so the parser is already ready for
keep-alive: several responses on one connection, or a pipelined pair.
The client API does not use that yet — it sends `Connection: close` and
does one request per connection, because that is the smallest correct
client. The keep-alive client is the natural next sub-step, and it is
where the framing bugs get interesting.

## 5. Valgrind

**0 errors, 0 leaks** on all seven binaries and every forked child (`make
valgrind`; 35 valgrind processes, exit status 0 — `--error-exitcode=1`
means any leak or invalid access would have failed the build). "All heap
blocks were freed -- no leaks are possible."

## 6. What was left on the table, deliberately

- **Chunked transfer-encoding.** A response can be framed in chunks
  instead of by a length, and the parser does not know how to read one.
  This is the gap Stage 65.6 named, seen from the other side.
- **Compressed responses** (`Content-Encoding: gzip`). The bytes are not
  text, and the parser would treat them as a body.
- **Redirects.** The client reports the 301/302 it was given; it does not
  follow them. Following them means reading the `Location` header and
  connecting again — and deciding what "again" means for a `POST`.
- **Keep-alive on the client side.** One request per connection today.
- **TLS.** Not in scope, and not going to pretend otherwise.
- **`Date`, `HEAD`, `POST` semantics** on the client side.

## 7. Provenance: the cap was blown a second time, and I am not going to dress that up

This stage ran inside a judgment cycle that started at **01:12:31Z**. The
gateway's 900-second cap expired at **01:27:33Z** and this stage shipped
at roughly **01:58Z** — thirty minutes over, and the second cycle in a
row.

The work survived, for the reason it survived last time: the code was
committed at `095d0f0` before this document existed, so the worst case is
"a green commit and one document to write."

But two cycles in a row is a pattern, and the pattern has a shape I can
name. **A stage does not fit in fifteen minutes.** Writing the code, the
tests, the close-out, and the four documentation updates is closer to
forty minutes of careful work, and no amount of intention makes that
fifteen. The 65.6 close-out said the fix is "a stage small enough to
fit" — and then I did it anyway, because the stage I picked was the one
the ladder pointed at.

So the rule I am actually learning is narrower: **the next stage must be
sized to the window, not to the ladder.** A ladder rung that does not
fit is a rung to split. That is the honest correction to my own
close-out, and it is the reason the pick below is deliberately smaller
than the design doc's 65.8.

The cost of two blown caps in a row is real: two judgment passes have
waited. The heartbeat is the thing that is supposed to be cheap, and I
have made it expensive twice. Worth saying plainly.

## 8. Next

**Stage 65.8 — DNS from scratch, in two halves.** The design doc's 65.8
is "build the query packet, parse the response, cache it", and that does
not fit the window. So the next rung is **the packets**: build a DNS
query for a name, and parse a DNS response — the header, the question
section, the answer section, and the name-compression pointers, which
are a framing problem of exactly the kind this component has been good
at. The UDP transport and the cache are the half after that.

### What would change the pick

Tom can redirect the target in one message — the pick is mine, the aim is
his. Concretely, any one of these moves the pick: a steer toward
finishing the client's keep-alive story (reuse one connection, several
requests) instead of moving on to DNS; a steer toward chunked
transfer-encoding, which is the gap named twice now; a steer toward
closing the sockets arc and returning to the clox ladder; a steer toward
the browser's one genuinely open item (the active-formatting-elements
list and the `<a>` special case); or a request to go straight to 65.9
and *measure* `fork()` against an event loop, which is where the arc was
headed all along.

The one thing that would **not** change the pick is silence. The ladder
is mine to walk, and the packets half of 65.8 is the next rung Tom has
already seen described.
