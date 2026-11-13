# Stage 65.35 — close-out: the client's half of `Expect`

**Branch:** `stage-65-sockets` · **Commits:** `ea531bd` (the stage),
`0d0dcc4` (the initializer fix valgrind forced) · **Tests:** 9 new
(component total **385**) · **Valgrind:** **267 clean summaries** (0 errors /
0 leaks) across all 37 binaries.

## What shipped

The client can now **write a head with an expectation**, **wait for the
server's answer to it**, and **send the body only when the server asked for
it**. Stage 65.33 gave the server the 100 Continue; nothing in the component
could produce the request that triggers it. This stage gives it one.

```
http_client_request_build_head(dst, len, method, target, host,
                                content_length, keep_alive,
                                hdr_name, hdr_value)
http_client_connection_write_head(c, method, target, content_length,
                                   keep_alive, expect_continue)
http_client_connection_expect_continue(c, timeout_ms, msg)   /* 1 | 0 | -1 */
http_client_connection_write_body(c, body, body_len)
http_client_request_expect(host, port, target, body, body_len,
                            timeout_ms, out)
```

The head builder is the one that writes the request line, the `Host`, the
`User-Agent`, the `Content-Length` and the `Connection` — the same bytes
`http_client_request_build_ex` has always written, because the plain builder
is now a call to it with no extra header. **One reader for the request head**
(65.24's and 65.26's lesson).

## The rules

**RFC 9110 §10.1.1 (Expect).** "A client MUST NOT generate a 100-continue
expectation in a request that does not include content." The refusal is
`EINVAL` and it happens **before anything reaches the wire** — the request is
refused before it is written, not after. Nothing is sent, so there is nothing
to clean up.

**RFC 9110 §10.1.1 (the wait).** "A client that sends a 100-continue
expectation SHOULD NOT wait for an indefinite period before sending the
content." The RFC forbids *indefinite* and names no number. **One second is
the house's number** (curl's), and it is a **parameter**, not a constant. The
deadline is measured on the **monotonic** clock, so the wait is the time
asked for and not the time the clock happened to lose.

**RFC 9110 §10.1.1 (the final answer).** "A client that receives a final
response before sending the content SHOULD NOT send the content." That
response — a 417, a 401, a 405 — **is the answer**, and the body is **not
wanted**: writing it onto a connection the peer may be closing is how a
client causes a reset.

**A timeout is different.** The RFC allows the content to be sent anyway
after the wait expires, and so does this client: the wait is a bound on the
*patience*, not a rule about the *body*. So the timeout path sends the body
and the request proceeds as an ordinary one.

**The three outcomes of the wait:**

| what the peer did | the client |
|---|---|
| sent a 1xx | **1** — the body still goes |
| sent a final response | **0** — the body does **not** go, and the final response is the answer |
| said nothing | **-1** with `errno = ETIMEDOUT` — the body goes anyway |

## The interim count moved into the connection

This is the finding worth keeping, and valgrind's part in it is the lesson.

The counter of interim responses read so far used to be a **local** in the
reader. The expect flow breaks that: the 100 is read in **one** call
(`expect_continue`) and the final response in **another**
(`read_response`). A peer that closed *between* them would have been reported
as `EPROTO` — "there was no response" — when in fact there **was** one, and
it was not the answer. Stage 65.31 had already ruled that case: a peer that
closes after an interim leaves the request unanswered, and the reason is the
same one a truncated body has — **the message did not arrive**
(`HTTP_REASON_INCOMPLETE`).

So the count lives in the **connection**. The knowledge lives with the
conversation, not with the bytes — the same shape as Stage 65.28's queue of
in-flight expectations.

**And then valgrind said something else.** The new field is read on every
path, and the tests that drive a socket they opened themselves build the
struct **by hand** (`c.fd = fd; c.port = port; memset(c.host, …);
parser_init(&c.parser); c.chunked_open = 0; c.n_inflight = 0;`) — which left
every field the component did not know about holding whatever was on the
stack. The new field was the first one actually **read** on those paths, so
it was the first to be caught:

```
Conditional jump or move depends on uninitialised value(s)
   at client_read_message (http_client.c:1255)
```

The fix is **not** a `memset` at 83 call sites. It is one function:
`http_client_connection_init()`, which zeroes the struct and initializes the
parser — and which `http_client_connection_open()` now uses too.
**The component owns the state it carries**, and a caller that hand-builds a
connection asks the component what a connection looks like before use.

The 83 sites are the tests; the one function is the fix.

## The proof that the tests discriminate

Three proofs, because the stage changed three things:

| experiment | result |
|---|---|
| the interim count reverted to per-call (`if (0)`) | **8/9** — the new "peer closes after the 100" test fails |
| the expectation removed from the head builder | **8/9** — the "the expectation is not on the wire" test fails |
| the initializer no longer zeroes the struct | valgrind reports the same **4 errors** again |

**The honest red, measured.** Like Stage 65.34, this stage was written in one
pass — the test file and the implementation went in together — so there was
**no red run at the time it was written**. The red was measured *afterwards*,
by reverting the behaviour to the pre-stage shape and re-running. That proves
the tests **discriminate**, not that they were written before the code, and it
is recorded here as what it is.

**Two of the three failures were mine, not the code's.** The first version of
the "the peer closes after the 100" test closed the peer socket while the
request head still sat unread in its receive buffer — and a socket closed
that way sends a **RESET**, not a **FIN**. The client saw `ECONNRESET`
("the connection was aborted"), which is a different event from a hangup.
The rule under test is about the hangup, so the test now **drains the head
first**, and the difference between the two events is written down in the
test.

## The oracle is our own server

The two halves of the component talk to each other, which is what Stage
65.34's oracle was and what made the mirror cheap:

1. `POST /echo` with `Expect: 100-continue` → the server sends
   **100 Continue**, the client sends the body, the server answers **200**
   with the body echoed ✓;
2. a peer that sends a **final response** instead of a 100 → the client
   returns **0**, and **the body is not written** ✓;
3. a peer that says **nothing** → the client returns **-1** with
   `ETIMEDOUT` after the timeout, and the body goes anyway ✓;
4. `Expect` on a request with **no content** → `EINVAL`, and **nothing goes
   on the wire** ✓.

The wait is bounded by a **regression guard**: the timeout test measures the
elapsed time and fails if the wait is not about the timeout asked for (250 ms
– 2000 ms for a 300 ms ask).

## The lessons

- **A wait needs a bound and a body needs a rule.** The two are different
  things and they answer different questions: the *wait* is bounded because
  the RFC forbids waiting forever; the *body* is withheld when the peer
  answered, and sent when the peer did not.
- **A final response before the body is the answer.** The body is not
  wanted — writing it onto a connection the peer may be closing is how a
  client causes a reset.
- **The RFC forbids "indefinite", not "long".** One second is the house's
  number, and it is a parameter.
- **The knowledge lives with the conversation.** The interim count had to
  move from a local into the connection, because the expect flow splits one
  conversation across two calls.
- **A component owns the state it carries.** Eighty-three hand-built structs
  were a hole in the component's contract, and the hole only opened once a
  field was actually read on those paths. The fix was one function, not
  eighty-three `memset`s.
- **A reset is not a hangup.** A socket closed with unread data in its
  receive buffer sends an RST; the test has to drain first if it means to
  test the FIN.

## What this stage does not do

- **`417 Expectation Failed`.** A client that receives a 417 *may* repeat the
  request without the expectation (RFC 9110 §10.1.1) — but a silent retry
  of a state-changing method is a policy the **caller** must own, so it is a
  later stage, deliberately.
- **The server's 417.** The server can refuse an expectation it will not
  satisfy; that is the other half of the same rule and is not here.
- **An expectation on a request with no body.** Refused (`EINVAL`) — the RFC
  says so, and there is nothing to wait for.
- **The `Expect` header on any other expectation.** Only `100-continue` is
  implemented; another expectation name is written as given and not
  interpreted.

## Next

Stage 65.36 — the **`417 Expectation Failed`** pair: the server that refuses
an expectation it will not satisfy, and the client that may repeat the
request without it. The client's half carries a policy question (a retry of
a state-changing method is the caller's decision), so the server's half is
the smaller stage and should go first.
