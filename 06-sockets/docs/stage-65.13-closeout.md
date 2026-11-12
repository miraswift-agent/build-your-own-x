# Stage 65.13 — the client decodes a chunked response — close-out

**Date:** 2026-11-12 · **Component:** `06-sockets` (C) · **Code:** this commit
**Branch:** `stage-65-sockets` · **Tests:** 5 new (component total **172**)

---

## 1. What shipped

The client learns HTTP's third framing. A response body can end in
three ways, and the headers say which:

| framing | how the reader knows |
|---|---|
| Content-Length | the header counts the bytes |
| the close of the connection | the body is whatever arrived before the peer hung up |
| **chunked transfer-encoding** | **the body carries its own boundaries, as text** |

`http_response` grew one field (`framed_by_chunked`), and the response
parser grew a chunk decoder plus the bookkeeping to accumulate a body
that arrives in pieces:

```c
if (out->framed_by_chunked) {
    return chunked_next(p, out, body_start);
}
```

The decoded body accumulates **in the parser**, and the caller's
`http_response` is a view of it — so a body that arrives in seven
pieces is still one body.

## 2. The oracle: the mirror of Stage 65.12's

Stage 65.12 asked *does our encoder produce Node's bytes?* This stage
asks *does our client read Node's bytes?* — **the same capture, the
opposite direction**.

The bytes are a real response from a server that is not ours: Node's
HTTP server, writing a body in three pieces, captured with `curl
--raw` so that curl did not decode the chunking for us.

```
$ node chunksrv.js &
$ curl -sS -i --raw http://127.0.0.1:18099/ -o node_resp.bin
$ xxd node_resp.bin
00000000: 4854 5450 2f31 2e31 2032 3030 204f 4b0d  HTTP/1.1 200 OK.
...
00000080: 7261 6e73 6665 722d 456e 636f 6469 6e67  ransfer-Encoding
00000090: 3a20 6368 756e 6b65 640d 0a0d 0a37 0d0a  : chunked....7..
000000a0: 4865 6c6c 6f2c 200d 0a38 0d0a 6368 756e  Hello, ..8..chun
000000b0: 6b65 6420 0d0a37 0d0a 776f 726c 6421 0a  ked ..7..world!.
000000c0: 0d0a 300d 0a0d 0a                        ..0....
```

**199 bytes**: a 156-byte head (Node's headers, including
`Transfer-Encoding: chunked`) and the 43-byte body. The fixture in
`tests/test_stage_65_13.c` is those bytes, verbatim.

The last test is the whole thing over a **real socket**: a forked child
replays Node's bytes, the client fetches, and the body must match byte
for byte.

## 3. The measurements

| | result |
|---|---|
| new tests | **5/5 passed** |
| component total | **172** (was 167) |
| valgrind | **0 errors, 0 leaks**, 13 binaries, exit 0 |

## 4. The findings

**A body can be framed three ways, and the headers say which.** The
client used to know two. The third was sitting in the response
headers the whole time, waiting to be read.

**Transfer-Encoding overrides Content-Length.** RFC 9112 §6.1: a
message carrying both is the classic request-smuggling shape, and the
safe reading is the one that trusts the coding that is actually on the
wire. The client ignores the length and reads the coding.

**The coding header is a list, and the last coding frames the body.**
`gzip, chunked` is a chunked body inside a gzip coding. (The client
does not *decompress* gzip — see §5.)

**A truncated chunked body is an error, not a short body.** If the peer
hangs up before the terminal chunk, the body was cut: the same rule the
Content-Length path already followed.

**The decoded body accumulates in the parser, not in the output.** The
parser keeps the head and hands the body bytes to the decoder as they
arrive, so a long body does not fill the parser's buffer — and a body
split across reads is still one body.

## 5. What this stage deliberately does *not* do

- **No keep-alive.** The client still sends `Connection: close`, and
  this stage did not make it reuse a connection. (That is the next
  stage — see §6.)
- **No gzip.** A `Transfer-Encoding: gzip, chunked` response is
  recognized as chunked, and the gzip layer is not undone.
- **No trailers sent by our server.** Stage 65.12's encoder writes the
  empty trailer section, which is the legal minimum; the client would
  read them if they were there.
- **No request smuggling beyond ignoring Content-Length.** The client
  is a client, not a proxy; the dangerous half of that problem is on
  the server side.

## 6. Next

**Stage 65.14 — the client reuses a connection (keep-alive).** The
server has supported HTTP/1.1 keep-alive since Stage 65.6; the client
still asks for `close` every time. This is the natural next stage
because chunked framing is exactly what makes it interesting: **a
framing that lets a message end before a connection does is a framing
that makes those two events different**, and a client that cannot tell
them apart will hang (waiting for bytes that will never come) or
truncate (stopping at the close when the body was already complete).

The oracle: a second request on the same socket must get a second
response, and the server must not have closed in between — observable
from the wire, not from our own opinion of it.

### What would change the pick

Tom can redirect this in one message: a steer to the `main` rename (the
OPEN question in `open-questions-for-tom.md` — a naming change, not a
capability); a steer to the browser's **outer-loop iteration cap** (the
documented single-behaviour difference from the spec — it needs a
decision about deliberately diverging from html5lib before it needs
code); a steer back to the clox ladder; a steer to the sockets
component's other deferred items (`Host` routing, `Date:`, TLS,
`SIGPIPE`, CNAME chasing, EDNS0, TCP fallback); or a request to spend
the time on the Vercel publish gate instead. Silence does not change
the pick: the board names the sockets component's chunked arc, and
Stage 65.13 was its client half.
