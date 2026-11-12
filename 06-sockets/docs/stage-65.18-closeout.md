# Stage 65.18 — the server reads the request body — close-out

**Date:** 2026-11-12 · **Component:** `06-sockets` (C) · **Code:** this commit
**Branch:** `stage-65-sockets` · **Tests:** 5 new (component total **210**)

---

## 1. What shipped

The board's claim, before the work:

> today the server answers a request and leaves its body in the socket,
> so on a keep-alive connection those bytes are parsed as the *next*
> request.

**Measured, the claim is half true.** The request parser already knows
`Content-Length` and already consumes the body bytes — it moves the
bytes that follow the body to the front of its buffer, so the next
request starts exactly where the body ended. **The framing was never
broken.**

What the server could not do was **act** on the body: the parsed body
sat in a struct field nobody read, and a `POST` got the same answer as
a `GET`.

So the server grows a route:

```
POST /echo  ->  200, the body, byte for byte
```

`/echo` is a route, like `/` and `/stream` — the path rules still hold,
and the routed server (Stage 65.16) answers it the same way.

## 2. The measurement that stays open: a *chunked* request body

The half of the claim that **is** true is about the other framing. A
request whose body is chunked is not read at all:

```
$ python3 -c '... POST /echo ... Transfer-Encoding: chunked ...
                5\r\nhello\r\n0\r\n\r\n ...'
HTTP/1.1 200 OK
Date: Thu, 12 Nov 2026 16:25:17 GMT
Content-Type: application/octet-stream
Content-Length: 0
Connection: close
```

`Content-Length: 0` — the body was not read — and the chunk bytes stay
in the socket, where the server parses them as the **next request** (a
parse error, and the connection ends). The decoder from Stage 65.11 is
right there, unused by the server. **That is Stage 65.19.**

## 3. The oracle: curl, which is not us

```
$ curl -sS -i -X POST --data-binary 'hello, body' http://127.0.0.1:$PORT/echo
HTTP/1.1 200 OK
hello, body

$ printf 'a\000\377b' > body.bin
$ curl -sS -i -X POST --data-binary @body.bin http://127.0.0.1:$PORT/echo
... 61 00 ff 62                      # a NUL and a 0xFF, byte for byte

$ curl -sS -i -X POST --data-binary 'hello, body' \
       http://127.0.0.1:$PORT/echo http://127.0.0.1:$PORT/
HTTP/1.1 200 OK
hello, bodyHTTP/1.1 200 OK
the document
```

The last one is the framing claim on the wire: two requests, one
connection, two correct answers. If the body were left in the socket,
the second answer would be the echo again.

## 4. The measurements

| | result |
|---|---|
| new tests | **5/5 passed** |
| component total | **210** (was 205), 20 binaries |
| valgrind | **0 errors, 0 leaks** on all twenty binaries, exit 0 |
| discrimination proof | the echo route removed → **0 of 5 pass**; restored, 5/5 |

The five tests: the echo answers with the body; the echo is byte for
byte; a body and then another request on the same connection; a body
that arrives in pieces (the head first, the body a moment later — the
server must *wait*, because a request that is not complete is not an
error); and a request without a body echoes nothing.

## 5. The findings

**A body is bytes, not a C string.** The echo carries the length with
the bytes, so a NUL and a `0xFF` survive the round trip. A server that
answered with `strlen(body)` would truncate at the first NUL — and a
body that contains one is not a rare thing; it is a protocol.

**The framing was never broken — the *use* was missing.** The board's
framing said the bytes were left in the socket. They were not: the
parser consumed them and moved the rest forward. The real gap was one
layer up, in the server, which had a body and did nothing with it. **A
prediction is cheap to test, and testing it is how the work gets
redirected** — the same lesson as Stage 65.15a, where the predicted gap
was on the wrong side of the connection.

**The claim's other half is true, and it is the other framing.**
`Content-Length` bodies were consumed; chunked bodies are not, and they
are left in the socket exactly as the board described. **A stage that
frames one framing correctly says nothing about the other one.**

**The echo is what makes the reading observable.** The server could
already consume a body; what it could not do was *show* that it had
one. A route that returns the body turns an invisible act into an
answer you can check — which is why the byte-for-byte test is worth
more than a test that only checks the connection stays in sync.

## 6. What this stage deliberately does *not* do

- **Chunked request bodies are not read** (measured above) — Stage
  65.19.
- **No `HEAD`** (RFC 9110 §9.3.2: the headers of the `GET`, and no
  body) and **no `405`** for a method the server does not implement —
  the two smaller conformance gaps, and the alternatives if the next
  stage proves too big for a judgment window.
- **The request-smuggling rule is not implemented**: RFC 9112 §6.1 says
  a server **MUST** answer `400` to a request carrying both
  `Transfer-Encoding` and `Content-Length`. Ours uses the
  `Content-Length` and ignores the coding — the shape that rule exists
  to close. Named, not silently patched.
- **The echo answers any method** — the server does not dispatch on the
  method at all (that is the `405` stage's work).

## 7. Next

**Stage 65.19 — the server reads a chunked request body.** The decoder
from Stage 65.11 is already written and already tested; the server
needs the mirror of what the *client* did in Stage 65.13 — feed the
decoder, pull the chunks, keep the bytes that follow the terminal
chunk as the start of the next message. The oracle is our own encoder
(Stage 65.12) driving our own server, with curl's chunked upload as
the independent check.

### What would change the pick

Tom can redirect this in one message: a steer to the `main` rename (the
OPEN question in `open-questions-for-tom.md` — a naming change, not a
capability); a steer to the browser's **outer-loop iteration cap** (the
one documented difference from the spec — it needs a *decision about
deliberately diverging from the oracle* before it needs code, and that
decision is Tom's); a steer to `HEAD` or `405` instead (smaller
conformance gaps ✓); a steer to the sockets component's other deferred
items (TLS, `SIGPIPE`, CNAME chasing, EDNS0, TCP fallback); a steer
back to the clox ladder; or a request to spend the time on the Vercel
publish gate instead. Silence does not change the pick: the board names
the sockets component's HTTP arc, and a chunked request body is the
framing hole it still has.
