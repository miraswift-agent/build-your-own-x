# Stage 65.20 — the client sends a chunked request body — close-out

**Date:** 2026-11-12 · **Component:** `06-sockets` (C) · **Code:** `1ded4f0`
**Branch:** `stage-65-sockets` · **Tests:** 8 new (component total **224**)

---

## 1. What shipped

The framing matrix had three corners filled and one empty:

| | sends chunked | receives chunked |
|---|---|---|
| **server** | 65.12 ✓ | 65.19 ✓ |
| **client** | **65.20 (this stage)** ✓ | 65.13 ✓ |

The client could *read* a chunked response and the server could *read* a
chunked request, but the client could not *send* one: its request
builder had one shape, and that shape ended with a `Content-Length`.

So the client grew the fourth shape, and **the shape of the API is the
lesson**:

```c
int http_client_request_build_chunked_head(char *dst, size_t dst_len,
                                           const char *method, const char *target,
                                           const char *host, int keep_alive);
int http_client_connection_write_chunked_head(http_client_connection *c,
                                              const char *method, const char *target);
int http_client_connection_write_chunk(http_client_connection *c,
                                       const void *data, size_t len);
int http_client_connection_write_chunked_end(http_client_connection *c);
```

The head goes out **first**, saying `Transfer-Encoding: chunked` and
naming no length; the body follows **in pieces** as they become
available; the terminal chunk says the body is done. There is
deliberately **no** one-shot convenience that takes a body of known
length and frames it chunked — for a body already in memory,
`Content-Length` is the honest choice. **The rule for choosing chunked
is about knowledge, not size** (Stage 65.12's lesson, on the way out
this time): chunked is for a length that is not known until the body
ends.

The framing itself was not re-invented: `http_chunk_encode` (65.12)
frames each chunk and `http_chunk_encoder_finish` writes the terminal
one, so the client and the server's `/stream` route now put **the same
bytes** on the wire.

## 2. A zero-length chunk is a terminator, not a payload

`http_client_connection_write_chunk(c, data, 0)` fails with `EINVAL`.
A zero-length chunk **is** the terminal chunk in HTTP, so a zero-length
*data* chunk would end the body by accident — and the terminal chunk
has its own call. One way to end a body, and it is not a payload.

## 3. The connection has to know whether a body is open

`http_client_connection` carries one new field, `chunked_open`, and the
connection **refuses** the shapes that would put two messages on the
wire at once:

| attempt | result |
|---|---|
| a chunk before the head | `EINVAL` — the peer would see a size where a request line belongs |
| a zero-length chunk | `EINVAL` — that is the terminal chunk |
| a normal request while a chunked body is open | `EINVAL` — it would land *inside* the body |
| a second chunked head while a body is open | `EINVAL` |

A rule that lives only in a comment is a rule the caller can break; a
rule the connection enforces is a rule the wire cannot contradict.

## 4. The oracle: a capture, not a memory

**Node's own bytes**, captured off the wire in Stage 65.19 and kept in
`tests/node_capture.h`: 135 bytes for a chunked upload — a 98-byte head
and 37 bytes of framing:

```
7\r\nHello, \r\nf\r\nchunked world!\n\r\n0\r\n\r\n
```

The client writes the same two chunks (`Hello, `, then
`chunked world!\n`) and the test compares the framing **byte for byte**
against the capture: the heads differ (Node's `Host` and header order
are its own), the **framing** does not. A peer captures the bytes
through a pipe, so what is checked is what went *out* — not what the
peer thought to answer.

**The live check is Node's HTTP server**, which decodes the upload into
one body:

```
node: transfer-encoding="chunked" content-length=null body_len=22
      body_hex=48656c6c6f2c206368756e6b656420776f726c64210a
client: status=200 body_len=22 body_hex=48656c6c6f2c206368756e6b656420776f726c64210a
```

Node saw **no `Content-Length`** (`null`) and reconstructed the body
our client sent — 22 bytes, identical in both directions. The binary
case survived byte for byte as well: `body_hex=6100ff62` (the NUL and
the `0xFF`), in both directions.

## 5. The measurements

| | result |
|---|---|
| new tests | **8/8 passed** |
| component total | **224** (was 216), 22 binaries |
| full suite | exit 0 |
| valgrind | **0 errors, 0 leaks** on all twenty-two binaries (121 clean summaries), exit 0 |
| discrimination proof | the chunk framing removed (the payload written raw) → **2 of 8 pass**; restored, **8/8** |

**The red state, for a stage that adds an API:** the test file does not
build against the code as it stood before this stage — the four new
functions do not exist, and `-Werror` turns the implicit declarations
into a build failure. That is the honest red for a new API: the tests
demand something the code cannot do yet.

**The two that still passed without the framing** are the two that do
not depend on it — the head builder (which only inspects the head) and
the empty body (which writes no chunk at all). The other six failed,
including the framing comparison against Node's bytes and the round trip
through the server.

**The test bug caught on the way:** the request-line check compared 22
bytes against a literal that is 21 bytes long (`"POST /echo HTTP/1.1\r\n"`
plus its terminator), so the test failed on its own arithmetic. The
fix is the standing rule — `sizeof(want_line) - 1`, never a counted
literal.

## 6. The findings

**A framing is a choice about knowledge, and the API is where that
choice becomes visible.** A client that *knows* its body's length
builds one request and writes it. A client that does not know yet
cannot: it must send a head that names no length, then pieces, then an
end. The four-call API is not ceremony — it is the shape of the
knowledge.

**The framing decides where a message ends; the connection decides
where the conversation ends.** The terminal chunk ends the *body*; the
connection is still open for the next request. The test writes a
chunked request and then a second request on the same socket and
requires two correct answers — if the chunk bytes were left in the
socket, the second answer would be the echo again.

**A body written in pieces is one body.** The head, a piece, a pause,
another piece, and the terminal chunk arrive as **one** body at the
server. This is the shape chunked exists for, and it is the reason the
client's API is a sequence of calls rather than one call.

**One way to write bytes.** The write loop that was inline in
`http_client_connection_write_request` is now a named helper
(`write_all`) with the same behaviour — `EINTR` is retried, a write
that moves **nothing** is `EPIPE` — and the new path uses it too.

## 7. What this stage deliberately does *not* do

- **No one-shot "send this body chunked" convenience.** A body already
  in memory should carry `Content-Length`; offering a chunked shortcut
  for it would teach the wrong rule.
- **No `Expect: 100-continue`.** A client that streams a large body
  usually wants the server's go-ahead first; it is a separate rule, not
  a detail of this one.
- **No chunk extensions on the send side** (`5;foo=bar`). The decoder
  handles them (65.11); the encoder emits none — same as 65.12.
- **The one-shot `http_client_get` still carries its own copy of the
  write loop**, and that copy does not treat a zero-byte write as an
  error the way the shared helper does. A small, named gap — not this
  stage's business, and not silently pretended away.
- **The loud half of the smuggling rule is still missing.** RFC 9112
  §6.1 says a server **MUST** answer `400` to a request carrying both
  a coding and a length. Ours still answers it — with the chunked body
  — rather than refusing it. This is the next stage.
- **`HEAD`** (RFC 9110 §9.3.2) and **`405`** for a method the server
  does not implement — the two smaller conformance gaps.

## 8. Next

**Stage 65.21 — the server answers `400` to a request that carries both
a coding and a length** (RFC 9112 §6.3 item 3). The *safe* half of that rule
shipped in 65.19 (the coding on the wire frames the body); the *loud*
half — refusing the request outright — is the last named gap in this
arc, and it is small: one more rule in the parser's branch, one test
that shows the difference between "answered with the chunked body" and
"refused".

### What would change the pick

Tom can redirect this in one message: a steer to **`HEAD`** or
**`405`** (the smaller conformance gaps ✓); a steer to the browser's
**outer-loop iteration cap** (it needs a *decision about deliberately
diverging from the oracle* before it needs code, and that decision is
Tom's ✓); a steer to the `main` rename (the OPEN question in
`open-questions-for-tom.md` — a naming change, not a capability ✓); a
steer to the sockets component's other deferred items (gzip coding
decompression, TLS, `SIGPIPE`, CNAME chasing, EDNS0, TCP fallback, the
one-shot write-loop gap named above ✓); or a request to spend the time
on the **Vercel publish gate** instead ✓. Silence does not change the
pick: the framing arc is closed, and the loud half of a security rule
is the gap it left behind.
