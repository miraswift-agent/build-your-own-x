# Stage 65.12 — chunked transfer-encoding, the encoder — close-out

**Date:** 2026-11-12 · **Component:** `06-sockets` (C)
**Code:** `6728b5e` (the encoder), `9a108e2` (the server that uses it)
**Branch:** `stage-65-sockets` · **Tests:** 9 new (component total **167**)

---

## 1. What shipped

**The encoder** — the mirror of Stage 65.11's decoder. Each call frames
exactly one chunk (`<hex size>\r\n<payload>\r\n`) and returns the bytes
to put on the wire, so nothing is buffered: the whole point of chunked
transfer-encoding is that the length is not known until the body is
finished.

```c
int http_chunk_encode(const void *data, size_t len, char *out, size_t out_cap);
int http_chunk_encoder_finish(char *out, size_t out_cap);   /* "0\r\n\r\n" */
```

**The chunked header block** — `http_response_build_chunked()`: the
same status line, Content-Type and Connection as `http_response_build()`,
but with `Transfer-Encoding: chunked` and **no** `Content-Length`.

**The server that uses it** — a `/stream` route that answers with a
chunked body written in three pieces.

## 2. The oracle: two implementations agreeing on bytes

Stage 65.11's capture (43 bytes off Node's HTTP server, taken with
`curl --raw`) is not just a fixture here — it is a **claim about two
implementations**. Handed the same three writes (`"Hello, "`,
`"chunked "`, `"world!\n"`), our encoder produces *exactly* Node's 43
bytes:

```
7\r\nHello, \r\n8\r\nchunked \r\n7\r\nworld!\n\r\n0\r\n\r\n
```

The server test reads the response off a **socketpair** — hermetic, no
address, no port — and compares the body with that capture. Our
server's bytes on the wire are the very bytes Node's server produced.

And the **inverted** check, live: our bytes served over a socket and
read by two decoders that are not ours.

```
$ curl -sS http://127.0.0.1:18099/ | od -c
0000000   H   e   l   l   o   ,       c   h   u   n   k   e   d       w
0000020   o   r   l   d   !  \n

$ curl -sS --raw http://127.0.0.1:18099/ | od -c
0000000   7  \r  \n   H   e   l   l   o   ,      \r  \n   8  \r  \n   c
0000020   h   u   n   k   e   d      \r  \n   7  \r  \n   w   o   r   l
0000040   d   !  \n  \r  \n   0  \r  \n  \r  \n

$ node -e "require('http').get('http://127.0.0.1:18099/', r => {...})"
BODY="Hello, chunked world!\n"
```

## 3. The measurements

| | result |
|---|---|
| new tests | **9/9 passed** |
| component total | **167** (was 158) |
| valgrind | **0 errors, 0 leaks**, 12 binaries, exit 0 |
| the capture | our encoder reproduces it byte for byte |
| the wire | our server's body == the capture, byte for byte |

## 4. The findings

**The strongest oracle is not "our encoder agrees with our decoder."**
It is *two independent implementations agreeing on bytes*. Node framed
those three writes one way; we frame them the same way; the bytes are
the same, and the test can say so in a `memcmp` instead of a paragraph.

**A framing is not a matter of taste.** It is a rule, and the evidence
that it is a rule is that two implementations produce the same bytes.

**The rule for choosing chunked is about knowledge, not size.** My own
stage plan said the server should answer with chunked "when the body is
worth chunking" — **that is the wrong rule**. A body of known length
should carry `Content-Length`; chunked is for a body whose length is
not known until it is done. The `/stream` route is honest because it
writes the body in pieces: the framing is decided *before* the length
is. The stage corrected the plan rather than the plan correcting the
stage.

**A zero-length chunk is the terminal chunk.** So the encoder refuses
`len == 0` (EINVAL): a stray empty write would otherwise end the body
by accident, and the only way to end a body is `http_chunk_encoder_finish()`.

**`--raw` is the opposite of what it looks like.** Mid-cycle I ran
`curl --raw` expecting a decoded body and got the framing instead —
`--raw` *disables* decoding, which is exactly how Stage 65.11's
capture was taken in the first place. When the oracle is bytes, the
flag that shows you the bytes is the one that turns the convenience off.

## 5. What this stage deliberately does *not* do

- **The client does not decode chunked responses yet.** Stage 65.7's
  client frames a response body by `Content-Length` or by the close of
  the connection; teaching it `Transfer-Encoding: chunked` is Stage
  65.13's work, and it has the Stage 65.11 decoder waiting.
- **We send no trailers.** A chunked response may carry trailers after
  the terminal chunk; the encoder's `finish()` writes the empty
  trailer section, which is the legal minimum.
- **The `/stream` route is a demonstration**, not a feature: it exists
  so that the chunked path is exercised over a real socket.

## 6. Next

**Stage 65.13 — the client decodes a chunked response.** The client
reads the response headers, and when they say `Transfer-Encoding:
chunked` it feeds the body bytes to the Stage 65.11 decoder instead of
counting `Content-Length` bytes. The oracle is the mirror of this
stage's: the client fetches from a **Node server that answers with a
chunked body** — an implementation that is not ours — and the body the
client hands back must match what the server wrote, byte for byte.

The sockets component's other deferred items stay on the board:
`Host` routing, `Date:`, TLS, `SIGPIPE`, CNAME chasing, EDNS0.

### What would change the pick

Tom can redirect this in one message: a steer to the `main` rename (the
OPEN question in `open-questions-for-tom.md` — nothing depends on the
answer, but it is the one question on the board); a steer to the
browser's **outer-loop iteration cap** (the documented single-behaviour
difference from the spec — it needs a decision about deliberately
diverging from html5lib before it needs code); a steer back to the clox
ladder; a steer to the other deferred sockets items; or a request to
spend the time on the Vercel publish gate instead. Silence does not
change the pick: the sockets component's chunked transfer-encoding is
half done, and the other half is the client.
