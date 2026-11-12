# Stage 65.30 — OPTIONS

**Branch:** `stage-65-sockets` (`~/build-your-own-x`)
**Code commit:** `d50d8e6`
**Component total:** 337 tests across 32 binaries
**Valgrind:** 220 clean summaries, 0 errors, 0 leaks

---

## 1. The rule

RFC 9110 §9.3.7:

> The OPTIONS method requests information about the communication options
> available for the target resource, at either the origin server or an
> intervening intermediary. This method allows a client to determine the
> options and/or requirements associated with a resource, or the
> capabilities of a server, without implying a resource action.

> If the request target is not an asterisk, the OPTIONS request applies to
> the options that are available when communicating with the target
> resource.

> A server generating a successful response to OPTIONS SHOULD send any
> header that might indicate optional features implemented by the server
> and applicable to the target resource (e.g., Allow), including potential
> extensions not defined by this specification. The response content, if
> any, might also describe the communication options in a machine or
> human-readable representation.

RFC 9110 §10.2.1 (the `Allow` field):

> The "Allow" header field lists the set of methods advertised as supported
> by the target resource. The purpose of this field is strictly to inform
> the recipient of valid request methods associated with the resource.

> An origin server MUST generate an Allow header field in a 405 (Method Not
> Allowed) response and MAY do so in any other response.

RFC 9110 §8.3:

> A sender SHOULD NOT generate a Content-Type header field in a message that
> does not contain content.

Three sentences, and they decide the stage: **what the response says**
(the `Allow` header, naming the target resource's methods), **what it
contains** (nothing), and **what it therefore must not claim** (a media
type it does not have).

---

## 2. What was left over from Stage 65.29

Stage 65.29 built a table of the methods each resource supports, and put
the list on a 405 response. It also made the list reachable in one other
way — the public `http_resource_allow()` — but only to a program holding
the pointer.

That left the table in an odd state: **the list a client most wants was
the one list it could not ask for.** `OPTIONS /` was a 501, because
OPTIONS was not a method this server knew. A client that wanted to know
what it could do with a resource had to provoke a 405 and read the header
off the refusal — which works, but only because the refusal happens to
carry the answer.

This stage makes the list askable.

---

## 3. What was built

### The dispatch: 200, the list, and no content

```c
if (strcmp(req.method, "OPTIONS") == 0 &&
    resource_methods(req.target) != 0u) {
    ...
    n = http_response_build_allow(resp, sizeof(resp), 200, "OK",
                                  "", NULL, 0, keep, date, allow);
```

Three decisions, each one a reading of the RFC text:

**200, not 204.** RFC 9110 §9.3.7 asks for "a successful response" and
the `Allow` header; it does not name a status code. 204 would have been
defensible — nginx answers `OPTIONS` with 204 — but RFC 9110 §8.6 says
a server MUST NOT send a `Content-Length` header field in any response
with a status code of 1xx or 204, and RFC 9112 §6.3 item 1 makes such a
response "always terminated by the first empty line after the header
fields, regardless of the header fields present in the message". That is
the framing rule Stage 65.28 had to teach the *client* for HEAD. Taking
it here would have pulled the 204/1xx/304 family into a stage about
OPTIONS. A 200 with `Content-Length: 0` is framed the ordinary way, so
**no client change was needed at all** — the client reads a zero-length
body, which it already knows how to do.

**The content is empty.** §9.3.7 says "The response content, *if any*,
might also describe the communication options in a machine or
human-readable representation" — so content is permitted, and the
standard format for it "is not defined by this specification". There is
nothing to send that the `Allow` header does not already say, so there
is no content.

**No `Content-Type`.** §8.3: a message with no content should not
describe a media type. This is a change to the response builder, and a
small one:

```c
if (content_type == NULL) {
    content_type = "text/plain";
}
if (content_type[0] != '\0') {
    ... build the Content-Type line ...
}
```

An empty string means "no `Content-Type`"; `NULL` keeps the old default,
so no existing caller had to change. One builder, not two — the same
discipline Stage 65.29 used for the optional `Allow` header.

### OPTIONS is in every resource's set

```c
#define HTTP_METHOD_OPTIONS 0x8u

{ "/",       HTTP_METHOD_GET | HTTP_METHOD_HEAD | HTTP_METHOD_OPTIONS },
{ "/stream", HTTP_METHOD_GET | HTTP_METHOD_HEAD | HTTP_METHOD_OPTIONS },
{ "/echo",   HTTP_METHOD_GET | HTTP_METHOD_HEAD | HTTP_METHOD_POST | HTTP_METHOD_OPTIONS }
```

This is the decision the stage turns on. After this stage the server
supports OPTIONS **for every resource it serves**, and §10.2.1 says the
`Allow` field "lists the set of methods advertised as supported by the
target resource". So OPTIONS is in the set, and it is in the list:

```
OPTIONS /        -> 200, Allow: GET, HEAD, OPTIONS
OPTIONS /stream  -> 200, Allow: GET, HEAD, OPTIONS
OPTIONS /echo    -> 200, Allow: GET, HEAD, POST, OPTIONS
```

The alternative was to keep OPTIONS out of the table and special-case it
in the dispatcher — which would have split the table in two: a mask for
the decision and a list for the header, with OPTIONS in one and not the
other. That is exactly the drift Stage 65.29 closed when it made one
table serve both. **One table, so the decision and the list cannot drift
apart** — and this stage is the proof: the 405's list grew by one name
because the table grew by one bit, and nothing had to be told twice.

### The target has to exist

`OPTIONS /nope` is a **404**, with no `Allow` header — the same rule
Stage 65.29 set for a missing target: there is no resource whose methods
could be listed, so there is no list to send. The OPTIONS branch falls
through to the ordinary 404 rather than growing a second 404 path.

### The method token is still case-sensitive

`options /` is a **501**, because §9.1's case-sensitivity applies to
every method, not only to the ones Stage 65.29 happened to test. A
lowercase method is not a misspelling of a known one; it is an unknown
one.

### The body is still framed and consumed

RFC 9110 §9.3.7 allows an OPTIONS request to carry content ("A client
that generates an OPTIONS request containing content MUST send a valid
Content-Type header field describing the representation media type"),
and the ordinary framing rules apply to it. The parser frames and
consumes the body, so the connection stays in sync and the response
keeps the conversation going. The request's content is not echoed: the
OPTIONS response describes the *resource's* options, not the request's
content — and a resource that has no semantics for request content (the
same reasoning Stage 65.28 applied to `HEAD /echo`) has none here
either.

---

## 4. The measurement

### Honest red — 3/12

```
  OPTIONS / is 200 with the resource's methods              FAIL: the status is not 200 OK
  the OPTIONS response carries no Content-Type              FAIL: a response with no content describes a media type
  OPTIONS /echo lists POST; OPTIONS /stream does not        FAIL: /echo does not list POST
  http_resource_allow agrees with the list on the wire      FAIL: / does not support GET, HEAD and OPTIONS
  OPTIONS /nope is 404, with no Allow header                OK
  options / is 501 -- the method token is case-sensitive    OK
  POST / is 405 with Allow: GET, HEAD, OPTIONS              FAIL: the Allow header does not list OPTIONS
  the OPTIONS response keeps the connection: a second request worksFAIL: the first response is not 200
  the OPTIONS request body was consumed: the next request worksFAIL: the first response is not 200
  our client asks OPTIONS and reads the Allow header back   FAIL: the status is not 200
  GET and HEAD are unchanged                                OK
  the routed server answers OPTIONS with the same list      FAIL: the status is not 200 OK
```

3/12 passed, nine failed. The three that passed are the three that had
to keep passing: the 404 for a missing target, the 501 for a lowercase
method, and GET/HEAD unchanged. Every failure is a behavior this stage
adds.

### Final — 12/12

### Full gate

- `make clean && make test` → exit 0
- **337 tests across 32 binaries**, 0 failures (up from 325/31)
- `make valgrind` → exit 0, **220** clean `ERROR SUMMARY: 0 errors`
  lines, no invalid reads/writes, no non-zero loss reports

---

## 5. The discrimination proofs

The stage changed three independent things — the dispatch branch, the
resource table, and the builder's `Content-Type` rule — so it needed
three proofs. Each one was applied to a copy of `src/http.c`, and the
source was restored and md5-verified afterwards.

**Proof A — the dispatch branch never fires** (`OPTIONS` → `OPTIONSX` in
the condition): **7/12 passed, 5 failed.** The failures were the five
tests that ask for the OPTIONS response itself: the status, the list,
the per-resource list, the client's read, and the routed server's read.

**Proof B — OPTIONS removed from the resource masks** (`|
HTTP_METHOD_OPTIONS` deleted from all three rows): **3/12 passed, 9
failed.** With OPTIONS recognized but unsupported by any resource, every
OPTIONS request became a 405 — and the 405's own list lost OPTIONS too,
which is the same table feeding both.

**Proof C — the builder always writes a `Content-Type`** (the empty-string
check replaced by a `NULL` check): **11/12 passed, 1 failed.** The single
failure was the test that names the rule: "the OPTIONS response carries
no Content-Type".

Restored: md5 verified, **12/12**.

---

## 6. The live oracles

### curl, against our server

```
$ curl -sS -X OPTIONS -D - http://127.0.0.1:18099/
HTTP/1.1 200 OK
Date: Thu, 12 Nov 2026 23:19:27 GMT
Allow: GET, HEAD, OPTIONS
Content-Length: 0
Connection: keep-alive
```

```
$ curl -sS -X OPTIONS -D - http://127.0.0.1:18099/echo
HTTP/1.1 200 OK
Date: Thu, 12 Nov 2026 23:19:27 GMT
Allow: GET, HEAD, POST, OPTIONS
Content-Length: 0
Connection: keep-alive
```

```
$ curl -sS -X OPTIONS -D - http://127.0.0.1:18099/nope
HTTP/1.1 404 Not Found
Date: Thu, 12 Nov 2026 23:19:27 GMT
Content-Type: text/plain
Content-Length: 14
Connection: keep-alive

404 Not Found
```

The `/echo` list is the one Stage 65.29 could not put on the wire:
`/echo` supports every method the server knew, so no known method could
be refused on it, so no 405 could carry its list. It is now the answer
to a question.

### Raw sockets — the exact bytes

```
b'HTTP/1.1 200 OK\r\nDate: Thu, 12 Nov 2026 23:19:33 GMT\r\nAllow: GET, HEAD, OPTIONS\r\nContent-Length: 0\r\nConnection: close\r\n\r\n'
```

No `Content-Type` line. No body. The head ends where the empty line
ends it and the length is zero — the ordinary framing, not the HEAD
framing.

### curl with a body on an OPTIONS request

```
$ curl -sS -X OPTIONS --data-binary 'hello' -D - http://127.0.0.1:18099/echo
HTTP/1.1 200 OK
Date: Thu, 12 Nov 2026 23:19:33 GMT
Allow: GET, HEAD, POST, OPTIONS
Content-Length: 0
Connection: keep-alive
```

The request carried a body; the response is the same response, and the
connection stayed open — the body was framed and consumed.

### Python's `http.server` — an independent server

```
$ curl -sS -X OPTIONS -D - http://127.0.0.1:18100/
HTTP/1.0 501 Unsupported method ('OPTIONS')
Server: SimpleHTTP/0.6 Python/3.12.3
```

Python's server does not implement OPTIONS and says so with a 501 — the
same answer it gives for DELETE. It is not an OPTIONS oracle, and that
is the point: **the implementations differ** (Python answers 501 here;
Apache answers 200 with an `Allow` list), and where they differ the RFC
decides which is right. The RFC asks for a successful response that
carries the applicable `Allow` header; the SHOULD is on the header, not
on a status code.

---

## 7. What this stage changed in an earlier stage's test

Stage 65.29's test asserted `Allow: GET, HEAD` for `POST /`, and
`http_resource_allow("/")` returning `"GET, HEAD"`. Both grew by one
name, because OPTIONS is now a method every resource supports — and
because RFC 9110 §10.2.1 says the field lists the methods supported by
the target resource, not the methods it supports *besides* OPTIONS.

The Stage 65.29 test file was updated in this commit. A later stage
refining an earlier stage's evidence is normal here: Stage 65.28's
case-sensitivity test changed its evidence the same way when Stage 65.29
made lowercase `head` a 501 instead of a served document.

---

## 8. The findings

- **A list you cannot ask for is half a feature.** Stage 65.29 built the
  table, the `Allow` header, and an accessor — and the accessor was the
  only way to get the list without provoking a refusal. The stage that
  finished the idea was not the one that started it.
- **One table, so the decision and the list cannot drift apart.** Adding
  OPTIONS to the masks changed the 405's list with no second edit. The
  alternative — a special case in the dispatcher — would have needed two
  places told the same thing.
- **"A response with no content" is a claim about the head, not just the
  body.** `Content-Length: 0` says the body is empty; `Content-Type`
  would have said what the body *would* have been. RFC 9110 §8.3 draws
  that line, and the builder now draws it.
- **The framing decides which family a response belongs to.** A 200 with
  `Content-Length: 0` is framed ordinarily, so the client needed no
  change; a 204 would have needed the HEAD machinery and pulled the
  204/1xx/304 family into the stage. Choosing the status was choosing
  the scope.
- **The method token is case-sensitive for every method, not only the
  ones a stage happens to test.** `options /` is 501 for the same reason
  `head /` is.
- **A proof that changes the behavior, not a string.** Proof A turned
  the branch off; Proof B removed the bit from the table; Proof C
  changed the builder's rule. Three changes, three proofs — the count
  follows the number of independent things the stage changed.

---

## 9. Named gaps

- **`OPTIONS *`** — the asterisk form "applies to the server in general
  rather than to a specific resource" (§9.3.7). The parser accepts it
  today and the server answers 404, because `*` is not a resource this
  server serves. Answering it properly means deciding what a
  *server-wide* list is — a third list, after the per-resource ones —
  and the RFC says the asterisk request "does nothing beyond allowing
  the client to test the capabilities of the server", so the honest
  answer may be a 200 with no `Allow` at all. That is a stage of its
  own.
- **`304`, `204`, and `1xx`** — the other three cases of RFC 9112 §6.3
  item 1, and the family this stage deliberately stayed out of.
- **A `Cache-Control` on the OPTIONS response** — §9.3.7 says
  "Responses to the OPTIONS method are not cacheable", which is a
  statement about caching rather than a requirement to send a header.
- **An OPTIONS request with content and no `Content-Type`** — the RFC
  puts that MUST on the *client*; the server does not enforce it.

---

## 10. Deliberate non-goals

- No `OPTIONS *`.
- No `Allow` on any response other than the 405 and the OPTIONS
  response (§10.2.1 says a server MAY send it elsewhere; the 404 has no
  resource to describe, and the 200 for a GET does not need it).
- No client-side change — the framing made one unnecessary.
- No `Vary`, no `Cache-Control`, no `Access-Control-Allow-*` (CORS is
  a separate story with a separate header).
- No second resource with a *different* method set beyond POST — the
  `/echo` list is the interesting one and it is now reachable.

---

## 11. Next — Stage 65.31

The framing family is one item from complete: **the other three cases of
RFC 9112 §6.3 item 1** — a `304`, a `204`, and a `1xx` response are all
"always terminated by the first empty line after the header fields,
regardless of the header fields present in the message". That is the
HEAD rule generalized, and it is the piece Stage 65.30 deliberately
avoided by choosing 200.

It will need the client's no-body machinery to learn the rule from the
*status* rather than from the request method, which is a different
shape from Stage 65.28's queue.

The alternative picks: the `OPTIONS *` form (a third list), the browser's
outer-loop iteration cap (which needs a decision about deliberately
diverging from html5lib before it needs code), the `main` rename (an
OPEN question for Tom; naming-only), the deferred sockets work (TLS,
`SIGPIPE`, CNAME chasing, EDNS0, TCP fallback, gzip coding, larger
benchmarks, a CPU-bound handler, a warm-up, the one-shot
`http_client_get` write-loop gap), or spending the cycle on the Vercel
publish gate instead of another stage.
