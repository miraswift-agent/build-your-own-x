# Stage 65.24 — the client refuses a response whose transfer coding it does not understand — close-out

**Date:** 2026-11-12 · **Component:** `06-sockets` (C) · **Code:** `7e8e318`
**Branch:** `stage-65-sockets` · **Tests:** 11 new (component total **262** across 26 binaries)

---

## 1. What shipped

The mirror of Stage 65.23, on the other side of the wire.

On the server side, a request that names a coding the server cannot
apply is answered **501**: the request is fine, the server is just not
capable. The client had no such answer. Today it reads the chunks,
hands the bytes back, and calls them the body — even when the coding in
front of the chunked one was never applied. The caller asked for a
document and got a compressed stream wearing the document's name.

So this stage ships:

```
GET /  (response: Transfer-Encoding: gzip, chunked)  ->  the client refuses it (ENOTSUP)
```

## 2. What the RFCs say, and what they do not

RFC 9112 §6.1 is explicit about the one coding every recipient must
have:

> A recipient MUST be able to parse the chunked transfer coding
> (Section 7.1) because it plays a crucial role in framing messages
> when the content size is not known in advance.

It is also explicit that a recipient may pass a coded body along
untouched — but only under a condition:

> Any recipient along the request/response chain MAY decode the received
> transfer coding(s) or apply additional transfer coding(s) to the
> message body, **assuming that corresponding changes are made to the
> Transfer-Encoding field value**.

So passing the coded bytes through is *allowed* — as long as the header
still names the coding. And it is allowed for a recipient **along the
chain**.

This client is the **end** of the chain, and its API has one field for
the body: `out.body` means "the body". A body it cannot decode is not
a body, so the client cannot deliver it, so the client fails the
response. The pass-through would need the API to say "this body is still
coded" — and the header is the only place that fact lives, so a caller
that forgets to look gets the wrong bytes under the right name. That is
the same failure the server had, one role over.

**The reason is the same reason the server used**: `ENOTSUP`, against
`EPROTO` for everything else. "Not implemented" is exactly what it is.

## 3. The refusal happens in the head

Stage 65.22's lesson, carried across: *the answer does not wait for
bytes we have already decided to refuse.* The client sees the coding in
the head and fails there — it does not wait for a body it has already
decided it cannot read.

The socket test makes that observable: the peer sends the head and then
**holds the connection open**, sending no body at all. If the client
waited for the body, its read would hit the two-second deadline and
fail with `EAGAIN`; the test asserts `ENOTSUP`, so "waited for the
body" and "refused the response" cannot both be true.

## 4. One scan, shared by both roles

The stage also closed a drift that had already cost us once.

The server's scan was fixed in Stage 65.22: the final coding is the
last **non-empty** element of the list, because RFC 9110 §5.6.1 says a
recipient must accept a list that contains empty elements. The client
was still using the **old** scan — the text after the last comma — so
`Transfer-Encoding: chunked,` read as "no coding", the body was
treated as framed by the close of the connection, and the client handed
back the **raw chunk framing** as the body.

Two scanners for one header is one scanner too many. The scan moved to
`src/coding.h` — internal to the component, shared by `http.c` and
`http_client.c` — and both roles read the list with the same code:

```c
http_coding_scan_list(value, role_understands_coding, &out);
```

The list is read once and answers both questions: *is the final coding
chunked?* and *does the list name a coding this role cannot apply?* The
predicate — "what can this program apply" — stays with the role: the
server's set is {chunked} and the client's set is {chunked}, and each
role owns its own answer, because the set of codings a program
implements is a property of the program, not of the header.

This is Stage 65.19's lesson applied: *a framing is a decoder, and a
decoder belongs to the bytes rather than to the role.* The scan decodes
a header value; it does not belong to either role.

## 5. The measurements

| | |
|---|---|
| new tests | **11/11** (`build/stage_65_24`) |
| component total | **262** across **26** binaries |
| full suite | `make test` → exit **0** |
| valgrind | **0 errors / 0 leaks** on all twenty-six binaries — **147** clean `ERROR SUMMARY` lines, `VG_EXIT=0` |
| discrimination proof A | with the **rule** removed: **5 of 11 pass, 6 FAIL**; restored: **11/11** |
| discrimination proof B | with the **old final-coding selection** restored: **10 of 11 pass, 1 FAIL** |
| live check | **Node** — its server, and its own client (§6) |

**The red run was honest and complete: 4/11.** The four that passed were
the ones that should: a chunked response is still decoded ✓, the coding's
name is case-insensitive ✓, a response with no coding is unchanged ✓, and
the client never *sends* a coding it cannot apply ✓.

**Two proofs, because the stage changed two things.** Proof A removes
the rule: six tests fail, including the socket one ("the failure is not
the refusal — the client waited for the body"). Proof B keeps the rule
and restores the old final-coding selection: only "an empty list element
does not hide the final coding" fails — which is the bug the shared scan
fixes, and the one the proof was built to isolate.

## 6. The oracle: Node, on both sides of the wire

A Node HTTP server that answers with `Transfer-Encoding: gzip, chunked`:

```
$ node /tmp/node_coded_server.js &
listening
```

**Node's own client** against it:

```
NODE CLIENT: status 200
NODE CLIENT: transfer-encoding = "gzip, chunked"
NODE CLIENT: body_len 22 body_hex 48656c6c6f2c206368756e6b656420776f726c64210a
```

Node dechunked the body — 22 bytes, "Hello, chunked world!\n" — and
handed them back as the body, with the gzip coding still applied. Node
never applied that coding, and it never said so: the bytes it returns
are what the wire carried.

**Our client** against the same server:

```
OUR CLIENT: refused (errno=95 ENOTSUP)
```

The server's side is unchanged by the refactor, checked live with curl:

| the request | the answer |
|---|---|
| `Transfer-Encoding: gzip, chunked` | **501** (65.23) |
| `Transfer-Encoding: gzip` | **400** (65.22) |
| no coding | **200** |

One honest note about the live server: it *names* the gzip coding
without applying one, so the bytes it sends are plain text. The client
cannot tell — the header is the only evidence — and that is exactly why
refusing is the honest answer rather than a technicality.

## 7. What valgrind caught: a test bug, and an asymmetry

The first valgrind run of the new binary reported **one error**:

```
Conditional jump or move depends on uninitialised value(s)
   at test_the_refusal_hands_back_no_body (test_stage_65_24.c:175)
```

The client's response parser does **not** zero its output on the error
path. The server's request parser does (`http_parser_next` memsets the
request before it parses), so a caller of the server's parser always
has a defined struct. The client's parser fills `out` as it goes — the
status line, the headers, the content length — and on the refusal path
it stops early, leaving the rest as it found it.

So the test was reading a struct the API does not define: **a test bug,
not a behavior gap** (the same shape as "a test that fails on its own
arithmetic"). The test now clears the struct first, and the contract is
written down where a caller will find it — in `http_client.h`:

> On the error path the output is NOT filled in — a caller must not read
> a response the parser refused. (The server's request parser zeroes its
> output; this one does not.)

The asymmetry is real and it is now named rather than accidental.

## 8. The findings

- **A coding you cannot apply is a coding you cannot undo.** The
  codings are applied in turn, so a body that still carries one of them
  is not the body the caller asked for. Reading the framing is not the
  same as understanding the content — Stage 65.23's lesson, one role
  over.
- **The end of the chain has no pass-through.** The RFC permits a
  recipient to pass a coded body along while the header still names the
  coding, but this client is the end of the chain and its API has one
  field for the body. Passing the bytes through would give `out.body`
  two meanings.
- **A recipient's obligation is the chunked coding** (RFC 9112 §6.1) —
  everything else is the recipient's choice, and this program chooses
  {chunked}.
- **The refusal belongs in the head.** The client does not wait for a
  body it has already decided it cannot read.
- **The reason travels in `errno`.** `ENOTSUP` against `EPROTO`: the
  same reason, the same carrier, on the other side of the wire.
- **One scan, shared by both roles.** The two scanners had drifted, and
  the drift was a bug — the client read `chunked,` as "no coding" and
  handed back the raw framing as the body. A decoder belongs to the
  bytes, not to the role.
- **A test that reads what the API does not define is a test bug.**
  Valgrind found it; the contract is now written down.
- **The client cannot tell whether the coding was actually applied** —
  the header is the only evidence, so the honest answer to "may I trust
  these bytes?" is no.

## 9. What this stage deliberately does *not* do

- **The gzip coding itself.** Implementing it would remove the 501 (on
  the server) and the refusal (on the client) for `gzip, chunked` — and
  not for `gzip` alone, which stays a 400. It would mean an inflate
  implementation, which is a project, not a stage.
- **The Content-Length rules** — RFC 9112 §6.3 item 5, a MUST, with two
  role-shaped halves: the server answers 400 and closes, and the user
  agent closes the connection and discards the response. The comma-list
  exception ("Content-Length: 5, 5" is one value of 5) is missing on
  both sides today. **This is the next stage.**
- **HTTP/1.0 with a `Transfer-Encoding`** — the framing is faulty and
  the connection closes. That rule is a MUST in RFC 7230 §3.3.3, which
  9112 superseded; 9112 §6.1 keeps the *concern* ("an HTTP/1.0 message
  received with a Transfer-Encoding is likely to have been forwarded
  without proper handling") without the normative sentence.
- **The client's parser zeroing its output** on the error path — a
  small, named asymmetry (see §7).
- No `HEAD`, no `405`, no TLS, no `SIGPIPE`.

## 10. Next

**Stage 65.25 — the Content-Length rules, in both roles**: a
`Content-Length` whose value is a comma-separated list of **equal**
values is one value (RFC 9112 §6.3 item 5's explicit exception), and a
`Content-Length` that cannot be used makes the message unusable — the
server answers **400** and closes, and the user agent **closes the
connection** and discards the response. Today the server refuses a
request the RFC says to accept (`Content-Length: 5, 5`), and the client
fails a response it refuses but leaves the connection open — a
connection whose framing is untrustworthy, still carrying the next
message.

### What would change the pick

- A steer to `HEAD` (RFC 9110 §9.3.2) or `405` — the smaller HTTP
  conformance gaps, and a change of pace from a run of refusals.
- A steer to the browser's **outer-loop iteration cap** (it needs a
  *decision about deliberately diverging from html5lib* before it needs
  code).
- A steer to the `main` rename — naming only; nothing depends on it.
- A steer to the deferred sockets items: TLS, `SIGPIPE`, CNAME chasing,
  EDNS0, TCP fallback, larger benchmarks, a CPU-bound handler, warm-up —
  and the one-shot `http_client_get` write loop, which still does not
  treat a zero-byte write as an error.
- A steer to the Vercel publish gate (the site has been stale for
  twenty cycles) instead of another stage.
- Silence does not change the pick.
