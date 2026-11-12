# Stage 65.11 — chunked transfer-encoding, the decoder — close-out

**Date:** 2026-11-12 · **Component:** `06-sockets` (C) · **Code:** `56add1e`
**Branch:** `stage-65-sockets` · **Tests:** 12 new (component total **158**)

---

## 1. What shipped

A decoder for chunked transfer-encoding — the framing where the body
draws its own boundaries, as text, inside the body:

```
<hex size>[;extensions]CRLF <size bytes>CRLF ... 0CRLF [trailers]CRLF
```

The API mirrors the Stage 65.6 request parser, because it has the same
shape of problem — bytes arrive in pieces and the decoder must say
"not yet" without treating that as a failure:

```c
void          http_chunk_decoder_init(http_chunk_decoder *d);
int           http_chunk_decoder_feed(http_chunk_decoder *d,
                                       const void *data, size_t len);
http_chunk_status http_chunk_decoder_next(http_chunk_decoder *d, char *out,
                                           size_t out_cap, size_t *out_len);
```

`http_chunk_status` is `HTTP_CHUNK_ERROR` (-1, the stream can never be
valid), `HTTP_CHUNK_NEED_MORE` (0, more bytes are owed — **not** a
failure), `HTTP_CHUNK_OK` (1, one chunk decoded) and `HTTP_CHUNK_DONE`
(2, the terminal chunk was seen).

## 2. The oracle is a capture, not a memory

The bytes in `tests/test_stage_65_11.c` came off a real socket. A Node
HTTP server — an implementation with nothing to do with this component —
wrote a body in two pieces, and curl was told `--raw` so that it would
**not** decode the chunking for us:

```
$ node chunksrv.js &
$ curl -sS --raw -D headers.txt http://127.0.0.1:18099/ -o capture.bin
$ xxd capture.bin
00000000: 370d 0a48 656c 6c6f 2c20 0d0a 380d 0a63  7..Hello, ..8..c
00000010: 6875 6e6b 6564 200d 0a37 0d0a 776f 726c  hunked ..7..worl
00000020: 6421 0a0d 0a30 0d0a 0d0a                 d!...0....
```

43 bytes, three chunks, and a terminal chunk. The framing in the test is
Node's; the test asserts that our decoder turns it back into
`"Hello, chunked world!\n"` — byte for byte.

## 3. The measurements

| | result |
|---|---|
| new tests | **12/12 passed** |
| component total | **158** (was 146) |
| valgrind | **0 errors, 0 leaks**, 11 binaries, exit 0 |
| the capture | decodes byte-for-byte, 3 chunks |

## 4. The findings

**A framing is not part of the message.** The captured bytes contain
`7\r\n`, `8\r\n` and `0\r\n`; the decoded body contains none of them.
The assertion is byte-equality of the *body*, which is the only
definition of "correct" that a framing can have.

**NEED_MORE is load-bearing here too.** `7\r\nHel` is not an error — it
is a connection that is still open. The decoder says NEED_MORE and asks
for the rest, and a chunk split across three feeds reassembles to the
same bytes as one that arrived whole.

**The chunk extensions are a trap.** `5;foo=bar\r\n` is legal, and a
decoder that parses hex "until a non-hex digit" stops at the `;` and
then treats the extension text as the payload boundary. The size parse
has to break at `;` explicitly.

**The terminal chunk is not the end of the bytes.** `0\r\n` may be
followed by trailers and a blank line, so "done" cannot be announced
until the trailer section's blank line has arrived. A decoder that says
DONE at `0\r\n` leaves the trailers sitting in the buffer for whoever
reads next — and if the connection then closes, they are never read at
all.

**The size is a number, and numbers can be absurd.** A hex size line
can claim more bytes than the buffer can hold (→ `ENOBUFS`) and can
overflow the size type (→ `EINVAL`), so both are checked before
anything is copied.

## 5. What this stage deliberately does *not* do

- **No encoder.** Nothing here writes a chunked body; this stage only
  reads one.
- **No integration.** The response parser does not yet say *which*
  framing a body uses — the caller must look at `Transfer-Encoding`
  versus `Content-Length` itself. That decision is Stage 65.12's.
- **No client use.** Stage 65.7's client frames a body by length or by
  the close of the connection; teaching it chunked is Stage 65.13.

## 6. Next

**Stage 65.12 — the encoder, and the server that uses it.** Build a
chunked response (size line, payload, CRLF, terminal chunk) and have the
HTTP server answer with `Transfer-Encoding: chunked` when the body is
worth chunking.

The oracle inverts, and that is the interesting part: **our bytes must
be decodable by an implementation that is not ours** — `curl --raw` and
Node's HTTP client both decode chunked bodies for free, so the check is
"curl and Node agree with us", not "we agree with us". Capture our
output with curl and decode it with the Stage 65.11 decoder, and decode
it again with Node's client. Both must produce the same body.

That stage also has to make the framing *decision*: `Content-Length` or
`Transfer-Encoding: chunked` is a choice with a rule behind it, and the
rule has to be written down before the code.

### What would change the pick

Tom can redirect this in one message: a steer to the `main` rename (the
OPEN question in `open-questions-for-tom.md` — nothing depends on the
answer, but it is the one question on the board); a steer to the browser's
**outer-loop iteration cap** (the documented single-behaviour difference
from the spec — it needs a decision about deliberately diverging from
html5lib before it needs code); a steer back to the clox ladder; a steer
to the C sockets component's other deferred items (`Host` routing,
`Date:`, TLS, `SIGPIPE`, CNAME chasing, EDNS0); or a request to spend
the time on the Vercel publish gate instead. Silence does not change the
pick: the board names chunked transfer-encoding as the next unit of work
in the sockets component, and Stage 65.11 is its first half.
