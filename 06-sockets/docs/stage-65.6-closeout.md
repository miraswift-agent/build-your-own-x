# Stage 65.6 — HTTP/1.1 from the wire (close-out)

**Date:** 2026-11-11 (CST evening) · **Branch:** `stage-65-sockets` · **Commit:** `bc84304`
**Component:** `06-sockets` (C) · **Predecessors:** 65.1 `56ce410`, 65.2 `301aad2`, 65.3 `c7a727e`, 65.4 `c2ec574`, 65.5 `9828192`

---

## 1. The lesson

**HTTP is text with rules, and the server is a state machine over bytes.**

Stage 65.5 built a binary framer: a 4-byte length prefix followed by a
payload, because that is the easy shape to frame. HTTP is the other
shape. The boundaries are written *into the text* — CRLF — and a request
is:

```
METHOD SP TARGET SP VERSION CRLF
Name: value CRLF
Name: value CRLF
CRLF
<body: however many bytes Content-Length says>
```

Everything hard about HTTP/1.1 is in those rules, and every one of them
is a boundary:

- the request line is three tokens separated by one space, and the version
  has to start `HTTP/` or you do not have an HTTP request;
- headers are `Name: value`, the name is case-insensitive, the value is
  trimmed of the spaces that follow the colon;
- `Content-Length` says how many body bytes follow, and **if they have not
  arrived yet the request is not ready** — that is not an error;
- HTTP/1.1 keeps the connection alive by default and HTTP/1.0 closes it
  by default, and either can override with `Connection:`.

The last rule is the one that earns its own stage later. Getting it
backwards does not produce an error — it produces a browser that hangs,
minutes later, with nothing in the logs.

## 2. What changed

New files: `src/http.h`, `src/http.c`. The socket layer is untouched; this
stage sits on top of it and uses 65.4's `sock_read_some` /
`sock_write_exact` (so the outcome of every transfer is data, and a
partial read is not an error) and 65.1's listener/accept.

**The parser** — `http_parser` is a buffer of bytes received, not yet
consumed:

```c
http_parser_init(&p);
http_parser_feed(&p, bytes, n);
http_parse_status st = http_parser_next(&p, &req);
/* HTTP_PARSE_OK | HTTP_PARSE_NEED_MORE | HTTP_PARSE_ERROR */
```

`NEED_MORE` is the load-bearing third answer. A request that has not
finished arriving is not broken, and a server that treats it as broken
closes connections it should have waited on. `ERROR` is reserved for
requests that will never be valid: a broken request line, a header with
no colon, a `Content-Length` that is not a number.

**The response** — `http_response_build()` writes a status line,
`Content-Type`, `Content-Length`, `Connection`, a blank line, and the
body. It refuses to write past the buffer it was given.

**The server** — `http_serve_connection(fd, root_body, &stats)` serves an
accepted connection: 200 with `root_body` for `/`, 404 for everything
else, and it keeps the connection open across requests when the client
asked for that (or when HTTP/1.1 says so by default).

## 3. The tests

21 tests: 18 about the rules, 3 over a real socket.

- the request line: method, target, version
- headers: case-insensitive lookup, trimmed values
- `Content-Length`: body parsed; **body not fully arrived is NEED_MORE,
  not an error**
- rejection: no version, a non-HTTP version, a header with no colon, a
  `Content-Length` that is not a number
- pipelining: two requests in one feed; the leftover bytes survive a parse
- keep-alive: HTTP/1.1 defaults to open, HTTP/1.0 defaults to closed,
  `Connection: close` overrides
- the response: a 200 with the right headers, a 404 that closes, and a
  refusal to overflow the buffer
- over a loopback socket: a request answered from the wire, a 404 from
  the wire, and **two requests on one connection**

The last three are the ones that make the stage a server rather than a
parser. The client is a forked child serving one connection over
127.0.0.1, and the client side reads a response by parsing the
`Content-Length` out of it — which is the same rule the server obeys,
seen from the other side.

## 4. The findings: one bug, three coats

**Three parser bugs, and they are the same bug.** The parser's whole job
is knowing where things end, and I got the end of the header block wrong
three different ways.

**Bug 1 — the underflow.** The header region is the bytes between the
end of the request line and the blank line. When a request has no
headers, the blank line *is* the request line's own CRLF followed by an
empty one — so the region is empty, and computing its length by
subtraction underflowed into a huge number. Every headerless request was
malformed.

**Bug 2 — the excluded offset.** The search for the request line's CRLF
stopped one byte short of the place where it sits when there are no
headers, so the parser reported "no request line" for every request of
the shape `GET / HTTP/1.1\r\n\r\n`.

**Bug 3 — the missing CRLF.** The blank line is `\r\n\r\n`, which *starts*
at the last header's own CRLF. So the header region runs to `hend + 2`,
not to `hend`. Stopping at `hend` cut the last header's terminator off
and made every request with headers malformed.

Bug 1 and bug 2 broke every headerless request. Bug 3 broke every request
with headers. **Nothing parsed until both shapes were right**, which is a
thing worth saying plainly: the test suite did not "mostly pass", it
passed 13 of 21 and then, after the third fix, 21 of 21.

The honest shape of this finding: it is Stage 65.2's finding one level
up. There, a *no-op* change was invisible to every behavioural test and
only a contract test could see it. Here, a *boundary* error is invisible
to every test that only checks the fields it cares about — the parse
returns a request with a plausible method and a plausible target, and
the test that checks the method passes while the whole protocol is
broken. The three bugs were found because the tests were written to
check *shapes* (a request with no headers, a request with headers) and
not just values.

**And one test bug of my own, which is the embarrassing kind.** The
keep-alive test asserted on `"HTTP/1.1 404 Not Found\r\n"` with a length
of 25. The literal is 24 bytes. The server was right and the test was
wrong. I argued with it for a while — "the response looks right, the
hex looks right, the strncmp returns 0 when I check it by hand" — and the
resolution was a hex dump: the assertion and the hand-check had *different
lengths*, and only the dump made that visible. **When the response is
right and the assertion fails, hex the bytes.** That is ten seconds, and
the argument I was having was several minutes long.

There is also a meta-finding in that: the test file I wrote first had
hardcoded feed lengths where `strlen()` belonged. Feeding 40 bytes from
a 27-byte literal reads past the end of the string — undefined behaviour
that happened to produce garbage rather than a crash. Fifteen feeds later,
all of them use `strlen()`. **Never hardcode the length of a string
literal you are about to feed.**

## 5. Valgrind

**0 errors, 0 leaks** on all six binaries and every forked child (`make
valgrind`; 10 valgrind processes, exit status 0 — `--error-exitcode=1`
means any leak or invalid access would have failed the build).
"All heap blocks were freed -- no leaks are possible."

The component total is now **76/76**: 8 (65.1) + 8 (65.2) + 8 (65.3) +
13 (65.4) + 18 (65.5) + 21 (65.6).

## 6. What was left on the table, deliberately

- **`Date:` header.** Every real HTTP response carries one. It needs a
  clock, and a test that asserts on a timestamp is a test that fails at
  midnight.
- **Chunked transfer-encoding.** A request without `Content-Length` but
  with `Transfer-Encoding: chunked` has a body whose length is *not*
  known in advance. The parser treats a bodyless request correctly and
  a chunked one incorrectly (as bodyless). Chunked is how a streaming
  client sends a body it is still producing; it wants its own stage.
- **`Host` routing.** The server answers `/` and 404s everything else. It
  does not route on `Host`, which is how HTTP/1.1 virtual hosts work.
- **`HEAD`, `POST` semantics, 100-continue, URL decoding.** The method
  is parsed and then ignored; the server answers 200 to any method on
  `/`. Honest for a first server, wrong for a real one.
- **TLS.** Not in scope, and not going to pretend otherwise.

## 7. Provenance: the cap was blown, and the discipline held

This stage ran inside a judgment cycle that started at **01:12:33Z**. The
gateway's 900-second cap expired at **01:27:33Z** and this stage shipped
at roughly **01:47Z** — the cap was exceeded by about twenty minutes.

It survived anyway, and the reason is the discipline that Stage 65.3's
close-out recommended and 65.4 adopted: **commit the code before writing
the close-out**. The code, its tests and its valgrind were committed at
`bc84304` before this document existed, so the worst case would have been
"a green commit and one document to write" rather than the orphan that
Stage 65.3's predecessor left behind.

That is the whole argument for the ordering. A timeout is not a
catastrophe when the last commit is a working one. **The commit is the
point of no return, not the last step.**

The cost of blowing the cap is honest and worth recording: the cycle ran
long, and the next judgment pass had to wait. The fix is not a bigger
timeout — that would trade a late stage for a late *heartbeat*, and the
heartbeat is the thing that is supposed to be cheap. The fix is a stage
small enough to fit, which is a discipline problem, not a config one.

## 8. Next

**Stage 65.7 — HTTP/1.1 client from the wire.** No `curl`, no libraries:
build the request, connect, write it, read the response, parse it. The
design doc's note is the reason it comes after the server: *the client
side is where framing bugs bite first*, because the client has to handle
a response whose length it does not know until it has read the headers.

### What would change the pick

Tom can redirect the target in one message — the pick is mine, the aim is
his. Concretely, any one of these moves 65.7 aside: a steer toward
chunked transfer-encoding (the gap named in §6); a steer toward closing
the sockets arc and going back to the clox ladder; a steer toward the
browser's one genuinely open item (the active-formatting-elements list
and the `<a>` special case); or a request to go straight to 65.9 and
*measure* `fork()` against an event loop, which is where the arc was
headed all along.

The one thing that would **not** change the pick is silence. The ladder
is mine to walk, and 65.7 is the next rung Tom has already seen
described.
