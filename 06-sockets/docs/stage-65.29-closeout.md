# Stage 65.29 — 405 Method Not Allowed

Code: `e46ecfb` · Branch: `stage-65-sockets` · Tests: **14 new** · Component: **325 across 31 binaries** · Valgrind: **206** clean summaries, 0 errors, 0 leaks

---

## 1. The rule

A request has a method, and the method says what the response is.
Stage 65.28 made the method visible in the response (the `HEAD`
treatment). This stage makes it **decide** the response.

**RFC 9110 §15.5.6:**

> The 405 (Method Not Allowed) status code indicates that the method
> received in the request-line is known by the origin server but not
> supported by the target resource. The origin server MUST generate an
> Allow header field in a 405 response containing a list of the target
> resource's currently supported methods.

**RFC 9110 §15.6.2** — the neighbouring rule, which this stage also
implements:

> The 501 (Not Implemented) status code indicates that the server does
> not support the functionality required to fulfill the request. This
> is the appropriate response when the server does not recognize the
> request method and is not capable of supporting it for any resource.

**RFC 9110 §9.1** — why the method set is a *list* and not a constant:

> Unlike distributed objects, the standardized request methods in HTTP
> are not resource-specific, since uniform interfaces provide for
> better visibility and reuse in network-based systems [REST]. Once
> defined, a standardized method ought to have the same semantics when
> applied to any resource, though each resource determines for itself
> whether those semantics are implemented or allowed.

And from the same section, why `get` is not `GET`:

> The method token is case-sensitive because it might be used as a
> gateway to object-based systems with case-sensitive method names.

(All four quotes fetched from `rfc-editor.org/rfc/rfc9110.txt` and read
from the section bodies, not from the table of contents.)

---

## 2. Three questions, in this order

The server asks three questions about a request, and the order matters
because each one subsumes the next:

| question | answer |
|---|---|
| Does the target resource exist? | no → **404**, and **no** `Allow` header — there is no resource whose methods could be listed |
| Does the server know the method at all? | no → **501** |
| Does **this** resource support the method? | no → **405** + `Allow` |

The 405 and the 501 are the two halves of the same sentence, and the
split between them is the one the RFC's own wording makes:

- **405** is for a method "not supported by **the target resource**" —
  the server can do it, just not here.
- **501** is for a method the server "is not capable of supporting
  **for any resource**" — the server cannot do it anywhere.

So on this server:

| request | answer | why |
|---|---|---|
| `POST /` | **405**, `Allow: GET, HEAD` | POST is implemented — on `/echo` — but not on `/` |
| `POST /echo` | **200** | the resource supports it |
| `DELETE /echo` | **501** | the server implements DELETE on **no** resource |
| `FROB /` | **501** | the server does not recognize the method |
| `get /` | **501** | the method token is case-sensitive; `get` is not `GET` |
| `PUT /nope` | **404** | the target does not exist, so the method never gets asked about |

### The ambiguity, named

§15.5.6 says "known by the origin server" and §15.6.2 says "does not
recognize the request method". A server could read those as "knows the
method *name*" (standardized) or "has semantics for the method
(implemented)". The two readings differ for a method that is
standardized but implemented nowhere — `DELETE`, here.

This component reads them the way the two sections' *other* phrases
read: §15.5.6 says "the **target resource**", §15.6.2 says "for **any**
resource". So the split is by capability, not by vocabulary: a method
the server can do somewhere is 405 here, a method it can do nowhere is
501. That is also what an independent implementation does — Python's
`http.server` implements `GET` and `HEAD` only, and answers **501** to
`POST`, `DELETE` and `FROB` alike (measured below).

The alternative reading — 405 for every method the server can
recognize — is not wrong so much as different, and the RFC does not
settle it in one sentence. It is recorded here rather than buried.

---

## 3. What was built

### The method table — one truth

```c
static const struct {
    const char *target;
    unsigned methods;
} resource_table[] = {
    { "/",       HTTP_METHOD_GET | HTTP_METHOD_HEAD },
    { "/stream", HTTP_METHOD_GET | HTTP_METHOD_HEAD },
    { "/echo",   HTTP_METHOD_GET | HTTP_METHOD_HEAD | HTTP_METHOD_POST }
};
```

One table holds both the mask the decision uses and the list the
`Allow` header carries, so the list on the wire cannot drift from the
mask that decided the answer.

`/echo` supports POST because the server has POST semantics there
(Stage 65.18's `/echo` reads the request body). `/` and `/stream` do
not: there is nothing for a POST to do to either of them.

### `http_resource_allow()` — the list, on its own

```c
int http_resource_allow(char *dst, size_t dst_len, const char *target);
```

The `Allow` header's value for a target: `"GET, HEAD"` for `/` and
`/stream`, `"GET, HEAD, POST"` for `/echo`, and the empty string for a
target that is not a resource this server serves.

It is public because the method set is a fact about the server, not
about one response, and a future `OPTIONS` (Stage 65.30) will need it
for exactly that reason.

### One builder, with an optional `Allow`

`http_response_build()` now delegates to
`http_response_build_allow(..., const char *allow)`. The header order,
the length, the date, the connection — all of it is the same code
path, so a response *with* an `Allow` header cannot drift from one
without. `Allow` sits between `Content-Type` and `Content-Length`.

### The refusal keeps the connection

This is the part that differs from Stages 65.21–65.23. Those refusals
happened in the **parser**: the message was refused before its body was
framed, so the connection could not be reused and had to be closed.

A 405 and a 501 here are decisions about a **request that was already
parsed**. The body was framed and consumed by the parser, so the
connection is still in sync and there is nothing to close. The
response says `Connection: keep-alive` when the request asked for it,
and the server keeps it — a header that said `close` while the server
kept the connection would be a lie the wire would disprove.

---

## 4. The measurement

### Honest red — 6/13

The test file was written before the implementation. Against the
pre-stage code: **6 of 13 passed**, and the seven failures were exactly
the new behaviors (the 405, the `Allow` header, the 501, the
per-resource distinction, the case-sensitivity, and the two
connection claims).

The final suite has **14** tests, not 13: the `DELETE` case split into
two — the 501 on the wire, and the per-resource method list read
through `http_resource_allow()`. The proof numbers below are against
the 14-test suite.

### Final — 14/14

```
  GET / is still answered with the document                 OK
  POST / is 405 with Allow: GET, HEAD                       OK
  POST /echo is still answered with the body                OK
  DELETE /echo is 501 -- the server cannot do DELETE at all OK
  the method set is per resource (/echo has POST, / does not)OK
  FROB / is 501 Not Implemented                             OK
  FROB /nope is 404, not 501                                OK
  PUT /nope is 404, not 405                                 OK
  get / is 501 -- the method token is case-sensitive        OK
  the 405 keeps the connection: a second request is answeredOK
  the 405 consumed the body: the next request is answered   OK
  the 501 keeps the connection: a second request is answeredOK
  our client reads a 405: status, body, and the Allow headerOK
  the 200 and the 404 carry no Allow header                 OK
```

### Full gate

- `make clean && make test` → **325/325 across 31 binaries**, exit 0.
- `make valgrind` → exit 0, **206** clean `ERROR SUMMARY: 0 errors`
  lines, no non-zero loss lines, no invalid reads/writes.

The forced clean rebuild matters: the Makefile does not track header
dependencies, and this stage changed a header.

---

## 5. The discrimination proofs

The stage changed three independent things, so it needed three
experiments. Each one removes a behavior and counts the failures.

| proof | change | result |
|---|---|---|
| A | the method rules removed (no resource has methods) | **6/14**, 8 fail |
| B | the 501 branch removed (an unknown method falls through to the 405) | **10/14**, 4 fail |
| C | the 405 closes the connection | **12/14**, 2 fail |

Proof A's eight failures are the 405, the 501, the per-resource list,
the case-sensitivity, and the three connection claims. Proof B's four
are the three 501 answers and the 501's connection claim — with the
branch gone, `DELETE` and `FROB` are answered 405. Proof C's two are
the two tests that put a second request on the socket after a 405.

**A proof that did not discriminate.** The first attempt at Proof C
changed the *header* the 405 carries (`Connection: close`) and left the
server's behavior alone. The suite stayed at **14/14** — the server
kept the connection while the header said it would close. The proof
failed, and what it exposed was a missing assertion: nothing checked
that the 405's header agrees with what the server does. The assertions
were added (`Connection: keep-alive` on the 405 and on the 501), and
the proof was re-aimed at the behavior (return after the refusal),
where it discriminates: **12/14**.

The lesson is the one the curriculum keeps meeting: a proof that does
not fail is not a proof, and a proof that does not fail is usually
pointing at an assertion that was never written.

**And a stale binary, caught by the final direct run.** After the three
proofs, the source was restored (md5 verified) but the *binary* was
not rebuilt — the last thing that compiled `build/stage_65_29` was
Proof C's patched source. The final direct run of the binary therefore
reported **12/14** on a tree that was perfectly correct, and the two
failures were Proof C's own. The committed source was right all along
(md5-checked after every restore); the artifact was not.

The fix was the house rule the curriculum already carries — a green run
can be a lie unless the tree was forcibly rebuilt — applied one step
further: **a proof's last build is the proof's binary, not the
restored one.** The whole component was re-run from clean afterwards:
`make clean && make test` → **325/325 across 31 binaries**, exit 0;
`./build/stage_65_29` → **14/14**; `make valgrind` → exit 0, **206**
clean summaries, no leaks, no invalid accesses.

A stale binary is a harness bug, not an implementation bug — and it is
the same shape as the failed proof above: something in the measuring
apparatus, not in the thing being measured. Two different ways for the
apparatus to lie, in one session.

---

## 6. The live oracles

### curl, against our server

```
--- POST / ---
HTTP/1.1 405 Method Not Allowed
Date: Thu, 12 Nov 2026 22:45:43 GMT
Content-Type: text/plain
Allow: GET, HEAD
Content-Length: 23
Connection: keep-alive

405 Method Not Allowed

--- POST /echo (with a body) ---
HTTP/1.1 200 OK
Content-Type: application/octet-stream
Content-Length: 5
Connection: keep-alive

hello

--- DELETE /echo ---
HTTP/1.1 501 Not Implemented
Content-Length: 20

--- FROB / ---
HTTP/1.1 501 Not Implemented

--- get / (lowercase) ---
HTTP/1.1 501 Not Implemented

--- PUT /nope ---
HTTP/1.1 404 Not Found
Content-Length: 14

--- GET / (unchanged) ---
HTTP/1.1 200 OK
Content-Type: text/html
Content-Length: 22

hello from our server
```

### Raw sockets — the connection claims

Two requests on **one** socket, the first a 405:

```
---- response ----
HTTP/1.1 405 Method Not Allowed
Allow: GET, HEAD
Content-Length: 23
Connection: keep-alive
BODY: 405 Method Not Allowed

---- response ----
HTTP/1.1 200 OK
Content-Length: 22
Connection: close
BODY: hello from our server
```

And a 405 that carried a request body, followed by a GET on the same
socket — the body was consumed, so the next message starts where it
should:

```
---- response ----
HTTP/1.1 405 Method Not Allowed
Allow: GET, HEAD
Connection: keep-alive
BODY: 405 Method Not Allowed

---- response ----
HTTP/1.1 200 OK
Content-Length: 22
BODY: hello from our server
```

### Python's `http.server` — an independent server

```
--- FROB / ---    HTTP/1.0 501 Unsupported method ('FROB')
--- DELETE / ---  HTTP/1.0 501 Unsupported method ('DELETE')
--- POST / ---    HTTP/1.0 501 Unsupported method ('POST')
--- HEAD / ---    HTTP/1.0 200 OK ... Content-Length: 187   (no body)
```

Python implements `GET` and `HEAD` only, so POST is a method it cannot
do **for any resource** — and it answers 501, exactly as this
component does for the same reason. That is an independent
implementation agreeing on the rule, not merely on the status code.

It cannot confirm the 405 half: Python has no method dispatch at all,
so it never produces a 405 and never sends an `Allow` header. The RFC
is the authority there.

---

## 7. What this stage changed in an earlier stage's test

Stage 65.28 pinned the claim "the method token is case-sensitive: `head`
is not HEAD" with the evidence "a method that is not HEAD may carry a
body" — the lowercase `head` used to be served with the document.

Under this stage the lowercase `head` is a method the server does not
recognize, so it is a **501**. The claim is unchanged; the evidence is
stronger: the request was refused outright, so it certainly was not
treated as a HEAD. The 65.28 test was updated to the new evidence and
says why in a comment.

This is the second time a stage's rule reached back into an earlier
stage's test (the framing refusals did it to the parser tests). The
curriculum's tests are one tree, so a later rule that changes an
earlier answer has to be recorded rather than papered over.

---

## 8. Named gaps

- **The `Allow` list for `/echo` cannot reach the wire today.** `/echo`
  supports every method the server knows (GET, HEAD, POST), so there
  is no known method that is refused on `/echo` — the only resource
  whose list would include POST can never produce a 405. The list is
  correct and tested through `http_resource_allow()`, but the 405 path
  that would carry it is unreachable for that resource. Stage 65.30
  (`OPTIONS`, RFC 9110 §9.3.7) makes it reachable: an OPTIONS response
  "SHOULD send any header that might indicate optional features
  implemented by the server and applicable to the target resource
  (e.g., Allow)".
- **`PUT`, `DELETE`, `OPTIONS`, `TRACE`, `CONNECT` are not implemented
  at all** — they are 501 on every resource. That is the honest answer
  for a server that does not do them.
- **No `Allow` header on the 404 and the 501** — correct: there is no
  target resource to list, and a 501 means the server does not do the
  method for any resource.
- **`OPTIONS *`** (the asterisk-form request target) is not handled —
  it will be a 404, since `*` is not a resource.
- **The client side needed no change.** `http_response` already exposes
  the headers, so the client reads a 405's `Allow` header with the
  existing API; the test uses it.

---

## 9. Deliberate non-goals

- No `OPTIONS` (Stage 65.30).
- No `PUT`/`DELETE` semantics.
- No `Vary`/`Cache-Control` — §15.5.6 says a 405 is "heuristically
  cacheable", and that is a caching decision this server does not
  make yet.
- No client-side 405 handling beyond reading the header: the client
  reports the status and the body like any other response.
- No change to the framing rules of Stages 65.21–65.27.

---

## 10. Next — Stage 65.30, `OPTIONS`

RFC 9110 §9.3.7: "The OPTIONS method requests information about the
communication options available for the target resource ... A server
generating a successful response to OPTIONS SHOULD send any header
that might indicate optional features implemented by the server and
applicable to the target resource (e.g., Allow)".

It is the natural companion to this stage: the same method table, the
same `Allow` list, and it makes the `/echo` list reachable on the
wire. It also completes the picture Stage 65.28 opened — the method
determines the response — by adding the method whose *purpose* is to
report the method set.

**What would change the pick:** the `304`/`204`/`1xx` family (the other
three cases of RFC 9112 §6.3 item 1) is equally small and would
complete the framing family; if the server grew a second POST-capable
resource, the `Allow` gap above would become reachable by wire and
would want doing first.
