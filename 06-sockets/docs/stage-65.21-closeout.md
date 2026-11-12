# Stage 65.21 — the server refuses a request whose framing is ambiguous — close-out

**Date:** 2026-11-12 · **Component:** `06-sockets` (C) · **Code:** `4c95159`
**Branch:** `stage-65-sockets` · **Tests:** 9 new (component total **232** across 23 binaries)

---

## 1. What shipped

Stage 65.19 shipped the **safe half** of the request-smuggling rule: a
request that carried both a `Transfer-Encoding` and a `Content-Length`
was framed by the coding on the wire, and the length was ignored. That
is what the RFC says *happens* — and the same paragraph says such a
message **"ought to be handled as an error."**

A server that implements only the first half leaves the shape open: two
recipients can disagree about where the body ends, and that disagreement
is what request smuggling is (RFC 9112 §11.2).

So the **loud half** shipped in this stage:

```
POST /echo  (Transfer-Encoding: chunked + Content-Length: 3)  ->  400 Bad Request
```

and the connection is closed after it — a connection whose framing is
ambiguous cannot be trusted with a second message.

The refusal lives in the **parser**, not in a route: the request never
becomes a request. The framing is what is wrong, so the framing is where
the answer is.

## 2. The rule, and where it actually is

**The citation was wrong before this stage.** The precedence rule was
quoted as *RFC 9112 §6.1* in Stages 65.13, 65.19 and 65.20. §6.1 is the
definition of the `Transfer-Encoding` field; the rule is **§6.3 item 3**,
inside *Message Body Length*:

> If a message is received with both a Transfer-Encoding and a
> Content-Length header field, the Transfer-Encoding overrides the
> Content-Length. Such a message might indicate an attempt to perform
> request smuggling (Section 11.2) or response splitting (Section 11.1)
> and ought to be handled as an error.

Two corrections to the board's earlier wording, both checked against
the RFC text this stage:

| board said | the RFC says |
|---|---|
| "RFC 9112 §6.1 says a server MUST answer 400" | the rule is **§6.3 item 3**, and it says **"ought to be handled as an error"** — not "MUST respond 400". Answering 400 is *how this server handles it as an error*. |
| "the loud half of the rule" | still fair, but the *whole* rule is one paragraph: the precedence sentence AND the error sentence. |

The **MUST respond 400** language in §6.3 belongs to the *other* two
items — a request whose final coding is not chunked (item 4) and an
invalid `Content-Length` (item 5). Those are named gaps below, and
item 4 is a MUST.

## 3. The safe half and the loud half are two roles, not two behaviors

The most useful thing this stage found is that the two halves of the
same paragraph are not two behaviors of one thing. They are the
behaviors of two **roles**:

| role | what it does with a message carrying both headers |
|---|---|
| a recipient that must process the message anyway | **trusts the coding** — the length is overridden (§6.3 item 3, first sentence). Our **client** does this; so does an intermediary that strips the length before forwarding. |
| an origin server that can say no | **refuses** — 400, and the connection closes. Our **server** does this now. |

So the client still trusts the coding on a response (Stage 65.13's
behavior, unchanged and tested), and the server now refuses the request.
Test 9 pins the client side so the asymmetry is deliberate rather than
accidental.

## 4. The rule is about presence, not about the coding's name

The conflict is **"a coding header AND a length header"** — two answers
to "where does the body end". It does not matter what the coding is
called:

```
Transfer-Encoding: chunked  + Content-Length: 3   ->  400
Transfer-Encoding: gzip     + Content-Length: 5   ->  400   (test 8)
Transfer-Encoding: chunked  + Content-Length: 0   ->  400   (test 7)
```

A `Content-Length` of **zero** is still a length: the conflict is the
*presence* of the two headers, not the size of the claim.

And the rule is narrow in the other direction too: a coding alone is
still answered (test 5) and a length alone is still answered (test 6).

## 5. The refusal does not wait for the body

The refusal happens **before any body is framed**, so the server
answers on the strength of the head alone. Test 3 sends the head and
never sends the chunks: the answer is the refusal.

This is a property worth having: the server does not sit there waiting
for bytes it has already decided to refuse — and it does not *read* a
body whose framing it distrusts.

**The test needed its own deadline to say so.** The first red run hung
there and the suite's global alarm killed the binary at test 3 — the
other six tests never ran. A hang is a poor failure mode, so that test
now sets a three-second receive timeout on its socket, and the hang
becomes a failure *message* instead of a killed binary. (Same lesson as
Stage 65.15b: **the harness can be the deadlock.**)

## 6. The measurements

| | |
|---|---|
| new tests | **9/9** (`build/stage_65_21`) |
| component total | **232** across **23** binaries |
| full suite | `make test` → exit **0** |
| valgrind | **0 errors / 0 leaks** on all twenty-three binaries — **128** clean `ERROR SUMMARY` lines (one per binary *and* per forked child), `VG_EXIT=0` |
| discrimination proof | with the rule removed: **3 of 9 pass, 6 FAIL**; restored: **9/9** |
| live check | a raw socket client and **curl** (§7) |

The red run was honest and complete: **3/9**, and the three that passed
were exactly the ones that should — the two "the rule is narrow" tests
and the client's safe half.

**The count moved.** Stage 65.19's sixth test asserted the server's safe
half ("a coding overrides a length"), which this stage deliberately
replaced. Rather than leave a test asserting a behavior the component
no longer has, that test was removed and the claim moved here as test 1
(inverted). 224 − 1 + 9 = **232**.

## 7. The oracle: curl, and a client that is not us

**curl** is the independent live check, and it confirms the normal paths
still work with a coding and no length:

```
$ printf 'hello from curl' | curl --http1.1 -H "Transfer-Encoding: chunked" --data-binary @- http://127.0.0.1:PORT/echo
> POST /echo HTTP/1.1
> Transfer-Encoding: chunked
hello from curl
```

(Curl sends `Content-Length` instead when it can — with a piped body it
buffered 15 bytes and knew the length, so the header was forced to
exercise the chunked path. The interesting fact is that the server
answers **both** shapes.)

The conflicting request cannot be produced by curl on purpose, so it was
sent by a **raw socket client** that is not our code:

```
POST /echo HTTP/1.1
Host: test
Content-Length: 3
Transfer-Encoding: chunked
Connection: close

5\r\nhello\r\n0\r\n\r\n
```

The answer, byte for byte:

```
HTTP/1.1 400 Bad Request
Date: Thu, 12 Nov 2026 18:54:08 GMT
Content-Type: text/plain
Content-Length: 16
Connection: close

400 Bad Request
```

`Connection: close` is the load-bearing part: the refusal ends the
conversation.

## 8. The findings

- **The citation is part of the claim.** A rule quoted from the wrong
  section is not the rule — and §6.3 is not §6.1. Three stages quoted
  the wrong one before this stage read the RFC text.
- **The normative level is part of the claim too.** "ought to be handled
  as an error" is not "MUST respond 400"; the MUST belongs to the two
  neighbouring items. The board's wording was stronger than the RFC's.
- **The safe half and the loud half are two roles.** A recipient that
  must process the message trusts the coding; an origin server that can
  refuse refuses. One paragraph, two answers, chosen by the role.
- **A refusal is a refusal.** On the wire, "conflicting framing" and
  "malformed request" are the same 400 — and that is correct. The
  distinction lives in the *reason*, not in a different status code.
- **A hang is a poor failure mode, and the suite's alarm is a poor
  place to discover it.** A test that is *expected* to hang needs its
  own deadline, or it takes the rest of the suite with it.

## 9. What this stage deliberately does *not* do

- **A request whose final coding is not chunked is still mishandled**
  (RFC 9112 §6.3 item 4 — a **MUST**: "the server MUST respond with the
  400 (Bad Request) status code and then close the connection"). Today
  `Transfer-Encoding: gzip` alone is answered 200 and the gzip bytes
  are left in the socket. **This is the next stage.**
- **A transfer coding we do not understand** → **501 Not Implemented**
  (RFC 9112 §6.1 — a SHOULD). Not implemented.
- **Duplicate or unequal `Content-Length` headers** → 400 (RFC 9112
  §6.3 item 5). The parser takes the first one.
- **The client's mirror** — a *response* carrying both headers. The
  client still trusts the coding (§3). Named, not done.
- **The intermediary role** — stripping the length and forwarding. Not
  a server's job; not implemented.
- The refusal and a malformed request are the same `HTTP_PARSE_ERROR`.
  If a caller ever needs to tell them apart, the status enum grows a
  fourth value.
- No `HEAD`, no `405`, no TLS, no `SIGPIPE`, no gzip coding
  decompression.

## 10. Next

**Stage 65.22 — the server answers `400` to a request whose final coding
is not chunked** (RFC 9112 §6.3 item 4 — a MUST, and the closest
neighbour of this stage's rule): a request with `Transfer-Encoding:
gzip` has a body whose length cannot be determined reliably, so the
server must refuse it and close the connection. Today it is answered
200 and the undecoded bytes are left in the socket, where they are
parsed as the next request — the same shape as the hole Stage 65.18
measured for chunked bodies.

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
