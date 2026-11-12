# Stage 65.16 — the server routes by `Host` — close-out

**Date:** 2026-11-12 · **Component:** `06-sockets` (C) · **Code:** this commit
**Branch:** `stage-65-sockets` · **Tests:** 6 new (component total **197**)

---

## 1. What shipped

The `Host` header is already on the wire — our client sends it, Node
sends it, curl sends it — and our server **ignored** it. That is not a
detail. A server with one document does not need it; a server with
several does: **the Host names the site the request is for**.

```c
typedef struct {
    const char *host;   /* the host name, without a port */
    const char *body;   /* the document served for that host */
} http_host_route;

int http_serve_connection_routed(int fd, const http_host_route *routes,
                                  size_t route_count, const char *default_body,
                                  http_server_stats *st);
```

`http_serve_connection` is **unchanged in behavior**: one document,
nothing to route.

The rules a host name has, and which are *not* the rules of a string:

| the Host says | the host is |
|---|---|
| `alpha.example` | `alpha.example` |
| `alpha.example:8080` | `alpha.example` — **the port is not part of the name** |
| `ALPHA.Example` | `alpha.example` — **a host name is case-insensitive** |
| `[::1]:8080` | `[::1]` — a bracketed literal keeps its brackets |
| `nowhere.example` | no route → the server's **default document** |
| *(absent or empty)* | **400** — a server with several sites cannot route |

## 2. The oracle: curl, which is not us

Six live requests against the running server:

```
$ curl -sS -i -H "Host: alpha.example"        http://127.0.0.1:$PORT/
HTTP/1.1 200 OK
alpha's document

$ curl -sS -i -H "Host: alpha.example:9999"   http://127.0.0.1:$PORT/
HTTP/1.1 200 OK
alpha's document

$ curl -sS -i -H "Host: ALPHA.Example"        http://127.0.0.1:$PORT/
HTTP/1.1 200 OK
alpha's document

$ curl -sS -i -H "Host: beta.example"         http://127.0.0.1:$PORT/
HTTP/1.1 200 OK
beta's document

$ curl -sS -i -H "Host: nowhere.example"      http://127.0.0.1:$PORT/
HTTP/1.1 200 OK
the default document

$ curl -sS -i -H "Host;"                      http://127.0.0.1:$PORT/
HTTP/1.1 400 Bad Request
400 Bad Request
```

The C tests write the **raw request bytes** on purpose: a client that
builds the `Host` header for us would be us agreeing with ourselves.

## 3. The measurements

| | result |
|---|---|
| new tests | **6/6 passed** |
| component total | **197** (was 191), 18 binaries |
| valgrind | **0 errors, 0 leaks** on all eighteen binaries, exit 0 |
| discrimination proof | routing turned off → **4 of 6 fail**; restored, 6/6 |

## 4. The findings

**A host name has rules, and they are not the rules of a string.**
`"example.com:8080"` is a *host:port*, and a server that routed on the
whole string would never match a route declared as `"example.com"` —
the bug would look like "routing does not work" rather than "the
routing key was wrong". Case-insensitivity is the same kind of trap in
the other direction: routing on a byte comparison would answer 200 with
the *default* document for a perfectly valid request, and the symptom
would be "some hosts work".

**A routing server and a single-document server have different needs,
and the difference is a rule, not a preference.** One document → the
`Host` is decoration. Several → the `Host` *is* the question. So the
routed server answers **400** when the `Host` is missing: a server with
several sites cannot route a request that does not say which site it is
for. The single-document server keeps its older behavior — changing it
would alter an existing API's contract silently.

**The default document is a decision, not an omission.** An unknown
`Host` gets the default — the shape nginx calls `default_server`: the
server has a fallback site, and a request for an unknown one lands
there rather than in an error.

**Routing is per request, not per connection.** The two-hosts test
writes both requests in **one write** on one socket, and each is
answered with its own document. The connection is a conversation; the
routing is a property of each message in it.

## 5. What this stage deliberately does *not* do

- **`/stream` is unchanged.** It is a fixed demonstration of the
  chunked framing; making it host-dependent is a different question.
- **The single-document server still accepts a `Host`-less HTTP/1.1
  request.** RFC 9112 §3.2 requires `400` — a named follow-up, because
  changing it would alter an existing API's contract.
- **No `Vary` header**, and deliberately: the response depends on the
  `Host`, and the `Host` is part of the cache key — not on a request
  header that varies the representation of one URL.
- **No TLS, no `Date:`, no `SIGPIPE` handling** — the deferred items on
  the board are unchanged.

## 6. Next

**Stage 65.17 — the server sends `Date:`.** RFC 9110 §6.6.1: an origin
server **MUST** generate a `Date` header field, and ours does not — a
real conformance gap, and the last header a well-behaved response
carries. The oracle is two implementations agreeing: Node's response
headers next to ours. The wrinkle is the clock: `strftime`'s `%a`/`%b`
are locale-dependent, so the day and month names are written by hand —
deterministic, and not at the mercy of the environment.

### What would change the pick

Tom can redirect this in one message: a steer to the `main` rename (the
OPEN question in `open-questions-for-tom.md` — a naming change, not a
capability); a steer to the browser's **outer-loop iteration cap** (the
one documented difference from the spec — it needs a *decision about
deliberately diverging from html5lib* before it needs code, and that
decision is Tom's); a steer to the sockets component's other deferred
items (TLS, `SIGPIPE`, CNAME chasing, EDNS0, TCP fallback); a steer
back to the clox ladder; or a request to spend the time on the Vercel
publish gate instead. Silence does not change the pick: the board
names the sockets component's HTTP arc, and the `Date` header is its
next conformance gap.
