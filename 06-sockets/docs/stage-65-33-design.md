# Stage 65.33 — design: the 100 Continue

**Status:** DESIGN ONLY — no code in this commit. Written 2026-11-12 in a
judgment pass whose wall-clock cap did not fit the implementation. The
close-out for Stage 65.32 said the shape of this stage "is worth writing
down before it is written"; this is that writing.

## The question

RFC 9110 §10.1.1: a client may send `Expect: 100-continue` to ask the
server to confirm it will take the body before the body is sent. A server
that receives a 100-continue expectation MUST send either a `100 (Continue)`
interim response or a final response.

## Why this stage is bigger than Stage 65.32

The serving loop waits for the **whole** request. `http_parser_next()`
returns `HTTP_PARSE_OK` only when the head *and* the body are present, and
`HTTP_PARSE_NEED_MORE` covers both "head incomplete" and "head complete,
body pending" — the caller cannot tell them apart.

The good news, read off the code: the parser does the head first (request
line, headers, and every head-level refusal of Stages 65.21–65.26), then the
body, and on `NEED_MORE` it leaves the buffer untouched. So the head is
*already* parsed when the body is missing — what is missing is a way to
**say** that.

## The change

1. **Parser — expose the head separately.** A new entry point
   `http_parser_head_next(p, out)` returns `HTTP_PARSE_OK` when the head is
   complete (the head-level refusals still fire — they are decided from the
   head), `HTTP_PARSE_NEED_MORE` when more head bytes are needed, and
   `HTTP_PARSE_ERROR` with the same errno semantics as today. `http_parser_next()`
   then becomes: *head complete? → body complete? → OK : NEED_MORE*. One
   reader for the head, used by both callers — the lesson of 65.24 and 65.26
   (a header both sides read belongs in one reader).

2. **Serving loop — the hook.** After the head is complete and before the
   body is waited for: if the request carries `Expect: 100-continue`, **and**
   the request has a body (a `Content-Length` or chunked), **and** the body
   has not all arrived → write the interim response, then keep reading.

3. **The interim response.** `HTTP/1.1 100 Continue\r\n\r\n` — no `Date`
   (RFC 9110 §6.6.1 forbids a `Date` in a 1xx; note the **opposite** of the
   304, which *keeps* the Date, §15.4.5), no `Content-Length` (RFC 9110 §8.6
   forbids it in a 1xx; RFC 9112 §6.3 item 1 terminates a 1xx at the empty
   line), no body. The builder's no-length mode came with Stage 65.32, but
   the 1xx also has to **drop** the `Date` that Stage 65.17 added — so the
   builder needs a 1xx mode, not merely a no-length mode.

## The decisions this stage has to make, written down

- **Expect on a request with no body.** Nothing to wait for, so no 100 — the
  final response alone satisfies the MUST. (RFC 9110 §10.1.1: a client MUST
  NOT generate the expectation in a request that carries no content.)
- **Expect on a request the server refuses** (404, 405, 501, 400). The final
  response satisfies the MUST; no 100. The 100 is for a request the server
  intends to serve.
- **Expect and the body already in the buffer.** No 100 — the server already
  has the content, and §10.1.1 lets it omit the interim response then.
- **Expect is hop-by-hop** (§10.1.1) and is not forwarded.
- **417 Expectation Failed** — the sibling rule, deliberately left for a later
  stage.

## The oracle

- **curl** sends `Expect: 100-continue` by itself for a body over 1024 bytes
  and waits about a second for the 100 before sending the body. Our server
  must give it the 100 and then the 200, and curl must not hang.
- **Node's** HTTP server answers `checkContinue` with a 100 by default.
- **Our own client already reads an interim response** — Stage 65.31's tests
  drive `100 Continue` then `200 OK` on the wire. So the client's half of
  *reading* a 100 is done; the client's half of *sending* `Expect` is not,
  and is the natural stage after this one.

## The tests (the plan)

1. Parser: head alone → the new API reports the head complete and the body
   pending, and `http_parser_next` still says `NEED_MORE`.
2. Server: `Expect` + a body → the wire shows `100 Continue` then the final
   response; the 100 carries no `Date` and no `Content-Length`.
3. `Expect` + no body → no 100.
4. `Expect` + the body already buffered → no 100.
5. No `Expect` → no 100 (the regression guard: every existing test).
6. `Expect` + a refused request → the final response, no 100.
7. The body still arrives and is served; the connection stays in sync.

## The risks

- The serving loop is the core of the component — the whole component must
  stay green (362 tests across 34 binaries, valgrind clean).
- **The Makefile does not track header dependencies** (the Stage 65.26
  lesson) → `make clean` before the full gate, or a green run can be a lie.
- The interim response is a message on a connection that already has a request
  in flight; the server's writer must not count it as the answer, and the
  client's in-flight queue must not treat it as a second response (Stage
  65.28's queue is where that lives).

## What would change this design

- If the **client** half goes first — sending `Expect` and waiting for the
  interim before the body — the parser change is identical and the server
  change is unchanged. The client half is the smaller stage and would let
  this stage's oracle be our own client rather than curl alone.
- If the "no 100 when the body is already here" rule turns out to disagree
  with curl or Node on the wire, that rule moves and the tests move with it.
