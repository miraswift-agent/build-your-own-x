# Stage 65.35 — design: the client's half of `Expect`

**Status:** DESIGN ONLY — no code in this commit. Written 2026-11-12 in a
judgment pass whose wall-clock cap did not fit the implementation. Stage
65.33's close-out said this stage "needs a **timer**, not just a client";
this is that writing, and it also records the RFC rules the client half has
to obey.

## The question

Stage 65.33 gave the **server** the 100 Continue: it reads the head, sees
`Expect: 100-continue`, and answers with an interim response before the
body arrives. The client can already **read** a 100 (Stage 65.31: a 1xx is a
complete message that is not the answer). What the client cannot do is
**ask** — send the expectation, wait for the interim, and send the body
only if the server wants it.

## Why this stage is the client **plus a clock**

RFC 9110 §10.1.1, requirements for clients:

> A client that sends a 100-continue expectation is not required to wait
> for any specific length of time; such a client MAY proceed to send the
> content even if it has not yet received a response. Furthermore, since
> 100 (Continue) responses cannot be sent through an HTTP/1.0
> intermediary, such a client SHOULD NOT wait for an indefinite period
> before sending the content.

So the wait is **bounded by the RFC**, and the number is not given — the RFC
only forbids *indefinite*. A client that waits forever is not conformant
and is a hang in practice: an HTTP/1.0 intermediary never forwards the 100,
and a server that intends to refuse may answer late or not at all.

The deadlock is not hypothetical. Stage 65.33's discrimination proof found
exactly this shape: remove the serving-loop hook and the wire test does not
fail — it **deadlocks**, because the client waits for the 100 and the
server waits for the body. A client half without a bound would reproduce
that deadlock against every server that does not answer.

**House choice: one second**, which is what curl waits. Recorded as a house
choice, not as an RFC requirement — and it is a *parameter*, not a constant,
so a caller can say "do not wait" or "wait longer".

## The change

1. **The head builder, split.** The request builder today writes the head
   *and* the body. The expect flow needs the head (carrying
   `Content-Length: N`) on the wire and the body later. New reader:
   `http_client_request_build_head(dst, dst_len, method, target, host,
   content_length, keep_alive, hdr_name, hdr_value)` writes the head only;
   `content_length < 0` means no `Content-Length`. The existing
   `http_client_request_build_with_header` **delegates** to it and then
   appends the body — one reader for the head (the lesson of 65.24, 65.26
   and 65.34: a header both sides read belongs in one place). The
   `Expect` header rides the 65.34 extra-header slot, so no new header
   machinery is needed and the slot's CR/LF and colon checks apply to
   `Expect: 100-continue` for free.

2. **A bounded wait.** New socket primitive `sock_wait_readable(fd,
   timeout_ms)` → 1 readable, 0 timed out, -1 error. `select()` — the same
   waiting idiom `sock_mux_poll` already uses, so the house keeps one way
   of waiting.

3. **The client flow**, at the connection level:

   - `http_client_connection_write_head(c, method, target, content_length,
     keep_alive, expect_continue)` — writes the head and pushes the
     in-flight queue entry (65.28's queue) **when the head goes out**,
     because the server's answer may be a final response that arrives
     before the body.
   - `http_client_connection_expect_continue(c, timeout_ms, http_response
     *msg)` — waits for **one** message:
     - returns **1**: a 1xx interim arrived (in `msg`) → the caller sends
       the body;
     - returns **0**: a **final** response arrived (in `msg`) → the server
       answered without the body; the caller must **not** send it;
     - returns **-1** with `errno == ETIMEDOUT`: no answer within the
       timeout → the caller sends the body anyway (the RFC's MAY);
     - returns **-1** on a read or parse error.
   - `http_client_connection_write_body(c, body, body_len)` — writes the
     body of the request whose head went out.

   The wait loop is the existing NEED_MORE loop with a deadline: parse →
   complete? → wait for readability with the time remaining → read →
   parse. **One reader**: `http_client_connection_read_response` becomes
   "read one message; if it is an interim, read the next one" — 65.31's
   interim handling moves *into* the one-message reader instead of
   remaining a second way of reading the same bytes.

4. **The one-shot**, mirroring 65.34's `http_client_get_conditional`:
   `http_client_request_expect(host, port, method, target, body, body_len,
   timeout_ms, out)` — connect, write the head, wait, send the body, read
   the final response.

## The decisions this stage has to make, written down

- **Expect on a request with no content.** "A client MUST NOT generate a
  100-continue expectation in a request that does not include content."
  `expect_continue` with `content_length == 0` → `EINVAL`, and **nothing
  goes on the wire** — the request is refused before it is written, not
  after.
- **The wait is bounded — one second by default, a parameter in the API.**
  The RFC forbids an indefinite wait and names no number; curl's second is
  the house's number.
- **On timeout: send the body anyway.** The RFC says the client MAY
  proceed without a response. Sending is the safe default — the request
  then behaves like an ordinary request and a server that never sent a 100
  reads the body as usual.
- **On a final response before the body: do NOT send the body.** The server
  has answered; the content is not wanted. That response *is* the answer
  (a 417, a 401, a 405). Writing a body onto a connection whose peer may
  be closing is how a client causes the peer's reset.
- **On a 417: do not retry automatically.** RFC 9110 §10.1.1: a client that
  receives a 417 "SHOULD repeat that request without a 100-continue
  expectation". A silent retry of a state-changing method is a policy the
  **caller** must own — so the component returns the 417 and the caller
  decides. The automatic retry is a later stage, if a caller asks for it.
- **The interim does not answer the request.** 65.31's rule holds: the
  in-flight queue advances only on the final response. The interim is
  consumed by the wait and does not touch the queue.
- **A timeout is an outcome, not a broken connection.** The connection is
  still good and the body still has to go, so `ETIMEDOUT` is reported by
  `expect_continue` and the *caller* decides to send. The one-shot does
  that for you.

## The oracle

- **Our own server** (Stage 65.33) — the 100 arrives, the body is served,
  the final response is 200. This is the stage where our own client
  becomes the oracle for the server's rule, instead of curl being the only
  thing in the room that can ask the question.
- **curl** — sends `Expect: 100-continue` by itself for a body over 1024
  bytes and waits about a second; the verbose output shows the
  expectation on the wire.
- **Node's** HTTP server — `checkContinue` sends a 100 by default, so the
  100 arrives from an independent implementation.
- **A raw peer** — the sharpest oracle, and the only way to reach the
  client's decisions at their edges: a socket that answers **417** without
  reading the body (the "do not send the body" path), and a socket that
  answers **nothing at all** (the timeout path).

## The tests (the plan)

1. The head carries `Expect: 100-continue` and `Content-Length: N`, and
   the body is **not** on the wire yet.
2. The server sends the 100 → the client sends the body → the final
   response is 200.
3. A final response before the body (417 from a raw peer) → the client
   returns it and **sends no body** (the peer sees EOF).
4. No response at all → the client waits its timeout, then sends the body
   anyway (the peer then answers 200).
5. Expect with no content → `EINVAL`, and nothing on the wire.
6. The interim does not advance the in-flight queue: a request pipelined
   after it still gets its own response.
7. **The wait is bounded** — the timeout path returns in about the
   timeout, not forever. This is the regression test for 65.33's lesson.
8. The head builder and the existing builder produce the same head bytes
   (one reader for the head).

## The risks

- **The deadlock.** A wait with no bound is the failure this stage exists
  to avoid, and it is the failure 65.33's discrimination proof produced.
  Test #7 is the guard.
- **The Makefile does not track header dependencies** (the 65.26 lesson) →
  `make clean` before the full gate, or a green run can be a lie.
- **The queue.** The in-flight entry is pushed when the *head* goes out,
  and the interim must not advance it. A mistake here is an answer
  attributed to the wrong request — the bug class of 65.28.
- **The buffer.** The interim may already be in the parser's buffer when
  the wait starts (65.14a's leftovers), so the wait must **parse before it
  polls**.

## What would change this design

- If the component should not own a timeout policy, the wait moves to the
  caller, which would poll and then call `send_body`. The house's answer
  so far: the component owns it, because the alternative is every caller
  re-implementing the deadlock.
- If a one-shot is enough — no connection-level API — the stage shrinks.
  But then a client cannot use Expect on a connection it is already using
  for other requests (pipelining, 65.15).
- If the 417 auto-retry moves in, the stage grows: a retry is a second
  request, and the connection's state after the first (the peer may have
  closed) has to be handled.
- If the server half of 65.33 turns out to answer a refusal only *after*
  reading the body — as the serving loop does today — then the raw-peer
  oracle is the only one that can produce the "final response before the
  body" case, and the client's own server cannot exercise it. That is a
  property of the oracle, not of the client.
