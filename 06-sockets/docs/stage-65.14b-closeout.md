# Stage 65.14b — the connection object: a second request on the same socket — close-out

**Date:** 2026-11-12 · **Component:** `06-sockets` (C) · **Code:** this commit
**Branch:** `stage-65-sockets` · **Tests:** 5 new (component total **182**)

---

## 1. What shipped

Stage 65.14a made the *parser* able to hold the bytes that follow a
response. This stage gives the *client* something to hold them in.

```c
typedef struct {
    int         fd;
    uint16_t    port;
    char        host[64];
    http_response_parser parser;
} http_client_connection;

int  http_client_connection_open(http_client_connection *c, const char *host, uint16_t port);
int  http_client_connection_request(http_client_connection *c, const char *method,
                                     const char *target, const char *body, size_t body_len,
                                     http_response *out);
void http_client_connection_close(http_client_connection *c);
```

The connection owns the socket, so a second request can be written on
it after the first response has been read. The request builder learned
to state the connection's fate:

```c
int http_client_request_build_ex(..., int keep_alive);
```

and the old one-request API keeps its old behavior — it still asks to
close, and `http_client_get` is unchanged.

## 2. The oracle: on the wire, not in our opinion of it

> a second request on the same socket must get a second response, and
> the server must not have closed in between.

The server says so itself. Our server (keep-alive since Stage 65.6)
answers a keep-alive request with `Connection: keep-alive` and keeps
the socket open. So:

- the response's `Connection` header must not say `close` — the **peer**
  is stating its intention, not us;
- the second request must be answered on the **same fd**;
- and a client that asked to close could not pass this test at all,
  because the server would hang up after the first response.

**The proof that the tests discriminate:** with the client changed to
send `Connection: close`, **2 of the 5 fail**:

```
two requests on one socket get two responses              FAIL: the second request failed
the server did not close between the requests             FAIL: the server said it was closing
```

With it restored: **5/5**.

The independent oracle is Node again: the same captured bytes, replayed
**twice** on one socket (`tests/node_capture.h`, now shared with Stage
65.13). The client must return Node's body twice — and the second
response is already in the parser when the second request is written,
which is Stage 65.14a's leftover primitive doing its job.

## 3. The measurements

| | result |
|---|---|
| new tests | **5/5 passed** |
| component total | **182** (was 177) |
| valgrind | **0 errors, 0 leaks**, 15 binaries, exit 0 |

## 4. The findings

**A connection is a conversation, not a message.** The object that owns
the socket is what makes a second turn possible. The parser alone was
not enough — a parser that can hold the leftovers still needs something
that can write the next request.

**The `Connection` header is how the peer learns a second request is
coming.** HTTP/1.1 keeps connections open by default, but saying so is
worth the bytes: the header is the contract.

**A close with an unread request on the socket is an RST — and an RST
discards the bytes the peer already sent.** This one cost real time.
The new test **passed alone and failed in the full suite**: the replay
child wrote Node's two responses and closed immediately, while the
client's request was still unread, so the kernel sent an RST and the
second response was thrown away. The fix was in the **peer**, not in
the assertion: the child now drains the requests before hanging up.

> **A test that passes alone and fails in the suite is a race, not a
> flake.** And the race was in the *harness*, not in the claim.

**The parser is asked before the socket.** The read loop calls
`http_response_parser_next` first and only reads when the parser has
nothing complete — because the bytes of the next response may already
be in hand (Stage 65.14a). Reading first would be wrong, not just
wasteful.

## 5. What this stage deliberately does *not* do

- **No pipelining.** The client waits for each response before writing
  the next request. Two requests written before either is read is the
  next stage (and it needs the server's parser to keep leftovers first
  — see §6).
- **No `Host` routing.** The client sends `Host:` and the server
  ignores it.
- **No TLS**, no `Date:` header, no `SIGPIPE` handling — the deferred
  items on the board are unchanged.
- **The client still cannot resolve a name** — a dotted-quad only.

## 6. Next

**Stage 65.15a — the server's parser keeps the bytes that follow a
response.** The mirror of 65.14a, on the server side: the server's
parser may be throwing away the bytes of the *next request* that
arrived in the same read. The first step is to **measure** — the
client's parser had exactly this gap, and it went unnoticed because the
client only ever made one request per connection. The server has the
same shape of blind spot.

Then **Stage 65.15b — pipelining**: two requests written before either
is read, answered in order. The oracle: the two responses come back in
the order the requests were written, on one socket.

### What would change the pick

Tom can redirect this in one message: a steer to the `main` rename (the
OPEN question in `open-questions-for-tom.md` — a naming change, not a
capability); a steer to the browser's **outer-loop iteration cap** (the
one documented difference from the spec — it needs a *decision about
deliberately diverging from html5lib* before it needs code, and that
decision is Tom's to make); a steer to the sockets component's other
deferred items (`Host` routing, `Date:`, TLS, `SIGPIPE`, CNAME
chasing, EDNS0, TCP fallback); a steer back to the clox ladder; or a
request to spend the time on the Vercel publish gate instead. Silence
does not change the pick: the board names the sockets component's HTTP
arc, and keep-alive is its next open seam.
