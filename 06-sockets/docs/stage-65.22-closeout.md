# Stage 65.22 — the server refuses a request whose final coding is not chunked — close-out

**Date:** 2026-11-12 · **Component:** `06-sockets` (C) · **Code:** `e4e160c`
**Branch:** `stage-65-sockets` · **Tests:** 11 new (component total **243** across 24 binaries)

---

## 1. What shipped

RFC 9112 §6.3 says how a message's body length is determined, and item 4
has three sentences that answer three different questions:

> **4.** If a Transfer-Encoding header field is present and the chunked
> transfer coding (Section 7.1) is the final encoding, the message body
> length is determined by reading and decoding the chunked data until the
> transfer coding indicates the data is complete.
>
> If a Transfer-Encoding header field is present **in a response** and
> the chunked transfer coding is not the final encoding, the message body
> length is determined by reading the connection until it is closed by the
> server.
>
> If a Transfer-Encoding header field is present **in a request** and the
> chunked transfer coding is not the final encoding, the message body
> length cannot be determined reliably; **the server MUST respond with the
> 400 (Bad Request) status code and then close the connection.**

The first sentence shipped in Stage 65.19 (the chunked request body). The
second is a rule about *responses* — a client rule, and the client
already behaves that way (§9 below). The **third** is the one we had not
implemented, and it is a **MUST**.

So this stage ships:

```
POST /echo  (Transfer-Encoding: gzip)            ->  400 Bad Request, then close
POST /echo  (Transfer-Encoding: chunked, gzip)    ->  400 Bad Request, then close
POST /echo  (Transfer-Encoding: gzip, chunked)    ->  200, framed by chunks
```

A request whose body length cannot be determined is a request whose body
boundary two recipients can disagree about — the shape request smuggling
is made of (RFC 9112 §11.2), and the same shape Stage 65.21 refused from
the other side (a request with *two* framings).

The refusal lives in the **parser**, beside 65.21's rule, because the
framing is what is wrong: the request never becomes a request, and the
answer does not wait for a body we cannot measure.

## 2. "Final" carries the whole rule

The rule is about the **last** coding in the list, and the word does all
the work:

| `Transfer-Encoding` | final coding | outcome |
|---|---|---|
| `gzip` | gzip | **400** — the body length cannot be determined |
| `chunked, gzip` | gzip | **400** — same coding, different position |
| `gzip, chunked` | chunked | **200** — the chunks draw the boundary |
| `chunked` | chunked | **200** — unchanged since 65.19 |
| `chunked,` | chunked | **200** — see §4 |

The first two rows are the sharpest pair: **the same two codings in the
other order give the opposite answer.** That is what "final" means, and a
server that looked at *any* coding instead of the last one would refuse
"gzip, chunked" — a request whose framing is perfectly determinate.

## 3. The rule lives in the parser, and it fires before the body

The refusal happens **before any body is framed**, so the server answers
on the strength of the head alone. Test 3 sends the head and never sends
a body: the answer is the refusal.

This is worth having for the same reason it was worth having in 65.21:
the server does not sit there waiting for bytes it has already decided
to refuse — and it does not *read* a body whose framing it distrusts.

**The test carries its own deadline** (a three-second `SO_RCVTIMEO`),
because this test is *expected* to hang if the server waits for a body.
A hang becomes a failure *message* in this test, not a killed binary
that takes the rest of the suite with it. (Same lesson as Stage 65.15b:
**the harness can be the deadlock.**)

## 4. The final coding has to be found correctly

A rule that fires on the final coding has to *find* the final coding
correctly, or it refuses requests the RFC says to accept. Two details
came out of reading the RFC rather than the memory:

**Empty list elements.** RFC 9110 §5.6.1:

> In other words, a recipient MUST accept lists that satisfy the
> following syntax:
>
>     #element => [ element ] *( OWS "," OWS [ element ] )

So a list may carry empty elements, and a **trailing comma does not make
the final coding empty**: `Transfer-Encoding: chunked,` names one
coding, chunked, and the request is framed by chunks. The old scan took
the text after the last comma, so it read `chunked,` as "no coding" —
and under this stage's rule that would have refused a valid request.

The scan is now `request_final_coding()`: walk the list, remember the
last **non-empty** element, trim the optional whitespace around it.
`request_is_chunked()` is a thin wrapper over it. **The rule and the
thing the rule reads were fixed together**, because a MUST that reads
its argument wrong is worse than no rule at all.

**Case.** A coding's name is case-insensitive (`Chunked` is chunked) —
that behavior was already there, and test 8 pins it so it stays.

## 5. The measurements

| | |
|---|---|
| new tests | **11/11** (`build/stage_65_22`) |
| component total | **243** across **24** binaries |
| full suite | `make test` → exit **0** |
| valgrind | **0 errors / 0 leaks** on all twenty-four binaries — **136** clean `ERROR SUMMARY` lines (one per binary *and* per forked child), `VG_EXIT=0` |
| discrimination proof A | with the **rule** removed: **6 of 11 pass, 5 FAIL**; restored: **11/11** |
| discrimination proof B | with the **old final-coding scan** (empty elements not ignored): **10 of 11 pass, 1 FAIL** |
| live check | **curl** and a raw socket client (§6) |

**The red run was honest and complete: 5/11.** The six that failed were
exactly the ones that should — the three server-level refusals, the
parser-level refusal, the "final coding is the last one" pair, and the
empty-list-element case. The five that passed were the ones that should:
the two "the rule is narrow" tests, the chunked-final list, the
case-insensitive name, and the empty header value.

**Two proofs, because the stage changed two things.** Proof A removes
the rule and shows the refusals disappear (5 fail). Proof B restores the
rule and puts the *old* token scan back, and the empty-list-element test
fails — so the refactor earns its place too, and is not riding on the
rule's coattails.

## 6. The oracle: curl, and a client that is not us

**curl** can produce this request after all — with a header it adds
itself and a body it does not send, so there is no `Content-Length` to
confuse the picture:

```
$ curl -v -H "Transfer-Encoding: gzip" http://127.0.0.1:PORT/echo
> GET /echo HTTP/1.1
> Host: 127.0.0.1:18080
> User-Agent: curl/8.5.0
> Accept: */*
> Transfer-Encoding: gzip
>
HTTP/1.1 400 Bad Request
Date: Thu, 12 Nov 2026 19:24:20 GMT
Content-Type: text/plain
Content-Length: 16
Connection: close

400 Bad Request
```

`Connection: close` is the load-bearing part: the refusal ends the
conversation.

The ordering case — the same two codings, chunked *first* — was sent by a
**raw socket client**, because that is the request that separates a rule
about the final coding from a rule about the presence of a coding:

```
POST /echo HTTP/1.1
Host: test
Transfer-Encoding: chunked, gzip
Connection: close

-> HTTP/1.1 400 Bad Request
```

And the paths that must keep working, live on the same server:

```
GET /                                          -> 200, "hello from our server\n"
POST /echo  (Transfer-Encoding: chunked)       -> 200, "Hello, chunked world!\n"
```

## 7. The findings

- **"Final" is the rule.** A coding list is a sequence, and only its
  last element frames the body. The same two codings in the other order
  give the opposite answer — which is the sharpest test the stage has.
- **A rule that reads an argument has to read it right.** The rule
  fires on the final coding, so the scan for the final coding is part
  of the rule. `chunked,` names one coding, chunked (RFC 9110 §5.6.1),
  and the old scan would have refused it. **The rule and the thing the
  rule reads were fixed together.**
- **Two proofs, because the stage changed two things.** Removing the
  rule and restoring the old scan are different experiments, and only
  the second one shows the refactor is load-bearing.
- **A refusal is still a refusal.** The status is 400 and the
  connection closes, exactly as in 65.21 — a request whose framing
  cannot be trusted gets the same answer whichever rule caught it. The
  distinction lives in the *reason*, not in the status code.
- **A hang is a poor failure mode, and the suite's alarm is a poor
  place to discover it.** A test that is *expected* to hang needs its
  own deadline.

## 8. What this stage deliberately does *not* do

- **A transfer coding we do not understand** → **501 Not Implemented**
  (RFC 9112 §6.1: "A server that receives a request message with a
  transfer coding it does not understand SHOULD respond with 501 (Not
  Implemented)"). Today `Transfer-Encoding: gzip, chunked` is answered
  200 and the body is returned **still gzip-coded** — we framed it
  correctly and then did not decode it. **This is the next stage.**
- **Duplicate or unequal `Content-Length` headers** → 400 (RFC 9112
  §6.3 item 5, a MUST). The parser takes the first one; a list of
  unequal values is not checked. (A `Content-Length` that is not a
  number *is* already refused with 400.)
- **HTTP/1.0 with a `Transfer-Encoding`** → the framing is faulty and
  the connection closes (RFC 9112 §6.1, a MUST). Not implemented.
- **The client's mirror** — a *response* whose final coding is not
  chunked is close-delimited (RFC 9112 §6.3 item 4, second sentence).
  The client already reads such a body until the connection closes ✓,
  but its own final-coding scan has the same empty-element quirk the
  server's had, and that was not fixed here (the client's rule is
  different, and it belongs to a client stage).
- No `HEAD`, no `405`, no TLS, no `SIGPIPE`, no gzip coding
  decompression.

## 9. Next

**Stage 65.23 — the server answers `501` to a request whose transfer
coding it does not understand** (RFC 9112 §6.1 — a SHOULD): a request
with `Transfer-Encoding: gzip, chunked` is framed by chunks, and today
we answer 200 with a body that is still gzip-coded. The decision tree
for the coding header is nearly complete, and this is its last branch:

| the request | the answer |
|---|---|
| a coding and a length | **400** (65.21) — two framings |
| final coding not chunked | **400** (65.22) — the length cannot be determined |
| final coding chunked, earlier coding unknown | **501** (next) — we cannot decode it |
| final coding chunked, all codings known | **200** ✓ |

### What would change the pick

- A steer to `HEAD` (RFC 9110 §9.3.2) or `405` — the smaller HTTP
  conformance gaps.
- A steer to the browser's **outer-loop iteration cap** (it needs a
  *decision about deliberately diverging from html5lib* before it needs
  code).
- A steer to the `main` rename — naming only; nothing depends on it.
- A steer to the deferred sockets items: gzip coding decompression,
  TLS, `SIGPIPE`, CNAME chasing, EDNS0, TCP fallback, larger benchmarks,
  a CPU-bound handler, warm-up — and the one-shot `http_client_get`
  write loop, which still does not treat a zero-byte write as an error.
- A steer to the Vercel publish gate (the site has been stale for
  seventeen cycles) instead of another stage.
- Silence does not change the pick.
