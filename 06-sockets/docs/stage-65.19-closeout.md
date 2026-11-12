# Stage 65.19 — the server reads a chunked request body — close-out

**Date:** 2026-11-12 · **Component:** `06-sockets` (C) · **Code:** this commit
**Branch:** `stage-65-sockets` · **Tests:** 6 new (component total **216**)

---

## 1. What shipped

Stage 65.18 shipped `/echo`: the server answers a request with the body
it read. But it could only read a body that arrived with a
`Content-Length`. The **other** framing — the one the header itself
announces — was still a hole, and Stage 65.18's close-out measured it:

> a chunked request body is not read at all. The server answers it with
> `Content-Length: 0` and leaves the chunk bytes in the socket, where
> they are parsed as the *next* request.

So the server grew the other framing. The decoder from **Stage 65.11** is
already written and already tested; what the server needed is the
**mirror** of what the *client* did in Stage 65.13.

```
POST /echo  (Transfer-Encoding: chunked)  ->  200, the DECODED body
```

and — the half that is easy to forget — **the bytes after the terminal
chunk are the start of the next message**, not part of this one.

## 2. A coding overrides a length

RFC 9112 §6.1: when a request carries both `Transfer-Encoding` and
`Content-Length`, the coding that is on the wire frames the body. The
**safe** reading trusts the coding — a length in a request that also
says "chunked" is exactly the shape that lets one end of a connection
and the other disagree about where the body ends.

**The client learned this rule in Stage 65.13; the server applies it
here.** The test makes the length a lie (`Content-Length: 3`, a
five-byte chunked body) and asks which one wins.

## 3. The oracle: a capture, not a memory

**Node**, captured off the wire — `http.request` with
`Transfer-Encoding: chunked`, no `Content-Length`, two writes:

```
POST /echo HTTP/1.1\r\nTransfer-Encoding: chunked\r\nHost: 127.0.0.1:44495\r\nConnection: keep-alive\r\n\r\n7\r\nHello, \r\nf\r\nchunked world!\n\r\n0\r\n\r\n
```

135 bytes, kept verbatim in `tests/node_capture.h` and replayed by the
tests. The decoded body is `Hello, chunked world!\n` — 23 bytes.

**curl**, captured the same way (curl 8.5), for a four-byte body:

```
POST /echo HTTP/1.1\r\nHost: 127.0.0.1:44921\r\nUser-Agent: curl/8.5.0\r\nAccept: */*\r\nTransfer-Encoding: chunked\r\nContent-Type: application/x-www-form-urlencoded\r\n\r\n4\r\na\x00\xffb\r\n0\r\n\r\n
```

174 bytes. **curl sends `Transfer-Encoding: chunked` and no
`Content-Length`** — measured, not assumed — and the live check against
the running server returns the body byte for byte (`61 00 ff 62`: the
NUL and the high byte survive).

## 4. The measurements

| | result |
|---|---|
| new tests | **6/6 passed** |
| component total | **216** (was 210), 21 binaries |
| valgrind | **0 errors, 0 leaks** on all twenty-one binaries, exit 0 |
| discrimination proof | the chunked branch removed → **1 of 6 passes**; restored, 6/6 |

The six tests: the chunked body is decoded and echoed; it is byte for
byte; a chunked body and then another request on the same connection get
two correct answers (the framing claim, on the wire); a chunked body
that arrives in pieces is still one body (the server must *wait*, since
a request that is not complete is not an error); an empty chunked body
echoes nothing; and a coding overrides a length.

## 5. The findings

**A framing is a decoder, and a decoder belongs to the bytes rather than
to the role.** The client's chunked *response* path (65.13) and the
server's chunked *request* path (this stage) are the same
`http_chunk_decoder` — one piece of code that knows how to read chunks,
used by both ends. The role decides who writes the bytes; it does not
decide how they are framed.

**The framing decides where a message ends; the connection decides where
the conversation ends.** The bytes after the terminal chunk are the next
message's bytes — the same rule Stage 65.14a found on the client's
response side, now on the server's request side. The decoder says what
it did not consume (`http_chunk_decoder_pending`), and the parser moves
those bytes to the front of its buffer.

**A named gap is a small stage.** Stage 65.18's close-out measured this
gap, quoted the symptom, and pointed at a decoder that already existed
— so this stage was a parser branch, not a subsystem. A gap that has
been *named* is a small piece of work; a gap that has only been *felt*
is a research project.

**A request that is not complete is not an error.** The body arriving
in pieces is the normal state of a connection that is still open. The
parser answers `NEED_MORE` and leaves the buffer alone; the decoder is
fed from the buffer afresh each time, so nothing is half-consumed.

## 6. What this stage deliberately does *not* do

- **The loud half of the smuggling rule is still missing.** The *safe*
  half is in (the coding wins), but RFC 9112 §6.1 also says a server
  **MUST** answer `400` to a request carrying both a coding and a
  length. Ours still answers it — with the chunked body — rather than
  refusing it. Named, not silently patched.
- **No `gzip` transfer coding.** The coding list's *last* coding frames
  the body, so `gzip, chunked` is read as chunked ✓; the gzip
  *content* is not decoded (it is not decoded in the response path
  either — a deferred item ✓).
- **No `HEAD`** (RFC 9110 §9.3.2) and **no `405`** for a method the
  server does not implement — the two smaller conformance gaps, and the
  alternatives if the next stage proves too big for a judgment window.

## 7. Next

**Stage 65.20 — the client sends a chunked request body.** The matrix
is nearly complete: the server *sends* chunked (65.12), the client
*receives* chunked (65.13), the server *receives* chunked (this stage),
and the client *sends* chunked is the last corner. The encoder from
Stage 65.12 already exists; what is missing is a request builder that
says "I do not know the length yet" — **the rule for choosing chunked is
about knowledge, not size** (65.12's lesson, on the way out this time).
The oracle: Node's HTTP server, which decodes a chunked upload — our
bytes must arrive as one body.

### What would change the pick

Tom can redirect this in one message: a steer to the **smuggling rule**
(the `400` for a request with both a coding and a length — small, and
the loud half of a security rule ✓); a steer to **`HEAD`** or **`405`**
(smaller conformance gaps ✓); a steer to the browser's **outer-loop
iteration cap** (it needs a *decision about deliberately diverging from
the oracle* before it needs code, and that decision is Tom's ✓); a steer
to the `main` rename (the OPEN question in
`open-questions-for-tom.md` — a naming change, not a capability ✓); a
steer to the sockets component's other deferred items (TLS, `SIGPIPE`,
CNAME chasing, EDNS0, TCP fallback ✓); or a request to spend the time on
the **Vercel publish gate** instead ✓. Silence does not change the
pick: the board names the sockets HTTP arc, and the client's missing
corner is the framing the arc is about.
