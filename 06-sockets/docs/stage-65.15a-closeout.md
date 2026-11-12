# Stage 65.15a — the measurement: does the server's parser keep the bytes that follow? — close-out

**Date:** 2026-11-12 · **Component:** `06-sockets` (C) · **Code:** this commit
**Branch:** `stage-65-sockets` · **Tests:** 4 new (component total **186**)

---

## 1. What was asked

Stage 65.14a found that the **client's** parser dropped the bytes of
the next response that arrived in the same read, and that nobody had
seen it — *because the client only ever made one request per
connection*. The server has the same shape of blind spot, with the next
**request** instead of the next response.

The board's next item was therefore a question, not a task:

> does the server's parser keep the bytes that follow a request it has
> already answered?

This stage measures before it writes.

## 2. The measurement: the answer is no gap — on this side

| level | what was measured | result |
|---|---|---|
| parser | two requests in one `http_parser_feed` | **both parse**, no second feed needed |
| parser | a request with a body, and the request after it | **both parse**, the body is intact |
| socket | two requests in one `write` | **two responses**, in order |
| socket | the connection between them | **not closed** (`Connection: keep-alive`) |

The code agrees: `http_parser_next` computes `rest = p->len - total`,
`memmove`s the leftovers down and sets `p->len = rest`; and
`http_serve_connection` asks the parser in an inner loop before it
reads the socket again.

**So the server was already ready.** The predicted gap is on the
**client** side: `http_client_connection_request` writes a request and
waits for its response before it writes the next one. That is Stage
65.15b.

**What shipped:** the four tests above — the measurement, pinned, so
the capability cannot regress silently while the client is taught to
use it. **4/4 passed**; component total **186**; valgrind **0 errors /
0 leaks** on all sixteen binaries.

## 3. The findings

**A prediction is cheap to test, and testing it is how the work gets
redirected.** The board named a gap on the server side; the
measurement put the gap on the client side. The stage did not become
"implement the server's leftovers" — it became "the server has them
already; the client does not".

**Two parsers that look alike can have different histories.** The
client's parser needed the leftover fix because its *chunked* path is a
different code path: the chunk decoder consumes the body bytes, so the
bytes after the terminal chunk end up inside the decoder rather than in
the parser's buffer (Stage 65.14a). The server's parser reads a
`Content-Length` body in place, so its leftovers were never in
trouble. Symmetry across the two parsers was not symmetry at all.

**A keep-alive server does not close, so a read that waits for EOF
waits forever.** The test hung, twice, for this reason. What ends the
read is the **event** — the second response arriving — not EOF. A
conversation that is still open has no end to wait for.

**The measurement's job is to redirect the work.** Its output is not
code; it is a smaller, truer next stage.

## 4. What this stage deliberately does *not* do

- **The client does not pipeline yet.** It still writes one request and
  waits for its response. That is Stage 65.15b.
- **No server changes.** There was nothing to change — that was the
  finding.

## 5. Next

**Stage 65.15b — the client pipelines.** The connection API separates
*writing a request* from *reading a response*, so two requests can go
out before either response comes back. The oracle: the responses come
back **in the order the requests were written**, on one socket — our
own server (already proven by this stage) and, independently, Node's
HTTP server with a captured pipelined response.

### What would change the pick

Tom can redirect this in one message: a steer to the `main` rename (the
OPEN question in `open-questions-for-tom.md` — a naming change, not a
capability); a steer to the browser's **outer-loop iteration cap** (the
one documented difference from the spec — it needs a *decision about
deliberately diverging from html5lib* before it needs code, and that
decision is Tom's); a steer to the sockets component's other deferred
items (`Host` routing, `Date:`, TLS, `SIGPIPE`, CNAME chasing, EDNS0,
TCP fallback); a steer back to the clox ladder; or a request to spend
the time on the Vercel publish gate instead. Silence does not change
the pick: the board names the sockets component's HTTP arc, and
pipelining is its next open seam — now that the server side is proven.
