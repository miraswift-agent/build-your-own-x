# Stage 65.32 close-out — the server's side: a response with no content

**Code:** `a54711e` — `feat(sockets): Stage 65.32 -- the server's side: a response with no content`
**Branch:** `stage-65-sockets` (`~/build-your-own-x`)
**Tests:** 14 new; component total **362** across **34** binaries
**Valgrind:** exit 0, **254** clean `ERROR SUMMARY: 0 errors` lines, 0 errors, 0 leaks
**Honest red:** **4/14** — the four that passed were the four that had to keep working

---

## 1. The rule, read rather than remembered

Stage 65.31 gave the client the framing rule for a response that has no content. This stage gives the server a response that uses it.

RFC 9110 §13.1.2 (**If-None-Match**):

> When an origin server receives a request that selects a representation and that request includes an If-None-Match header field, the origin server MUST evaluate the If-None-Match condition per Section 13.2 prior to performing the method.

> A recipient MUST use the weak comparison function when comparing entity tags for If-None-Match (Section 8.8.3.2), since weak entity tags can be used for cache validation even if there have been changes to the representation data.

> To evaluate a received If-None-Match header field:
> 1. If the field value is "\*", the condition is false if the origin server has a current representation for the target resource.
> 2. If the field value is a list of entity tags, the condition is false if one of the listed tags matches the entity tag of the selected representation.
> 3. Otherwise, the condition is true.

> An origin server that evaluates an If-None-Match condition MUST NOT perform the requested method if the condition evaluates to false; instead, the origin server MUST respond with either a) the 304 (Not Modified) status code if the request method is GET or HEAD or b) the 412 (Precondition Failed) status code for all other request methods.

RFC 9110 §15.4.5 (**304 Not Modified**):

> The server generating a 304 response MUST generate any of the following header fields that would have been sent in a 200 (OK) response to the same request: Content-Location, Date, ETag, and Vary

> A 304 response is terminated by the end of the header section; it cannot contain content or trailers.

RFC 9110 §8.8.3 (**ETag**):

> An entity tag consists of an opaque quoted string, possibly prefixed by a weakness indicator. … If an origin server provides an entity tag for a representation and the generation of that entity tag does not satisfy all of the characteristics of a strong validator (Section 8.8.1), then the origin server MUST mark the entity tag as weak by prefixing its opaque value with "W/" (case-sensitive).

RFC 9110 §8.8.3.2 (**Comparison**):

> "Strong comparison": two entity tags are equivalent if both are not weak and their opaque-tags match character-by-character.
> "Weak comparison": two entity tags are equivalent if their opaque-tags match character-by-character, regardless of either or both being tagged as "weak".

RFC 9110 §8.6 (**Content-Length**):

> A server MAY send a Content-Length header field in a 304 (Not Modified) response to a conditional GET request; a server MUST NOT send Content-Length in such a response unless its field value equals the decimal number of octets that would have been sent in the content of a 200 (OK) response to the same request.

RFC 9110 §13.2.1 (**When to Evaluate**) — the two rules that say *when not*:

> A server MUST ignore all received preconditions if its response to the same request without those conditions, prior to processing the request content, would have been a status code other than a 2xx (Successful) or 412 (Precondition Failed).

> … a server MUST ignore the conditional request header fields defined by this specification when received with a request method that does not involve the selection or modification of a selected representation, such as CONNECT, OPTIONS, or TRACE.

## 2. What this stage is, and what it is not

**Is:** the server's half of the no-content family. The document route now carries a validator, a conditional GET is evaluated, and a false condition is answered with a 304 — a response with no content and no length.

**Is not:**

- **Not the 412.** The rule has two branches, and only one of them can fire in this server. The 412 is for "all other request methods", and the only method here that selects a representation is GET or HEAD: `POST /` is a 405 answered before the precondition is even looked at, and `POST /echo` is a resource whose representation is the request's own body, so it has no validator and its method does not select a representation. The 412 branch is therefore unreachable **by construction** — named here rather than written.
- **Not a cache.** Nothing is stored, nothing expires, and there is no `Cache-Control`. The validator is a claim about the bytes, not a caching policy.
- **Not a `Last-Modified`.** RFC 9110 §15.4.5 says a 304 "MUST generate any of the following header fields that would have been sent in a 200 (OK) response to the same request" — this server sends `Date` and `ETag`, so those are the two the 304 must carry. It sends no `Last-Modified`, so the 304 sends none either.
- **Not a validator on every route.** `/echo`'s representation is the request's own body and `/stream`'s is chunked; neither has a stable representation to validate. Only the document route has one.

## 3. Three claims, and where each one lives

**The validator is weak, and that is a claim about the generator.** The tag is an FNV-1a hash of the representation's bytes. A hash that can collide is not a strong validator (RFC 9110 §8.8.1: "A collision-resistant hash function … is also sufficient" — FNV-1a is not collision-resistant), so §8.8.3 *requires* the `W/` prefix. The weakness indicator is not decoration; it is the honest description of what the generator can promise.

**The comparison is weak, and that is the rule's choice, not the server's.** §13.1.2 says "A recipient MUST use the weak comparison function". So `W/"x"` and `"x"` are the same tag here — the difference between them is exactly the difference between a cache that stored the tag and one that stored the tag without its weakness indicator.

**A precondition is evaluated only where it can change the answer.** §13.2.1 twice: a 404 wins over a precondition, and a method that does not select a representation ignores it entirely. Both are implemented by *ordering*, not by extra branches — the 405/501 and OPTIONS branches are already ahead of the route, so a precondition never reaches a response it should not have changed.

## 4. The implementation

**`src/etag.h` (new).** Three things, each about bytes rather than about a role:

| | |
|---|---|
| `http_etag_weak(dst, len, data, n)` | the weak tag: `W/"fnv1a64-<16 hex>"` |
| `http_etag_opaque(tag, len, &out)` | the opaque tag inside an entity tag; `-1` if the value is not one |
| `http_etag_weak_equal_n(a, alen, b)` | the weak comparison (RFC 9110 §8.8.3.2) |
| `http_if_none_match_matches(value, etag)` | the field-value evaluation (§13.1.2) |

**The list split knows about quotes.** An entity tag's opaque-tag is a quoted string, and RFC 9110 §5.6.1 lets a list element be a quoted-string — so a comma can be *inside* an entity tag. `ETag: "a,b"` is legal, and a naive split on commas would read it as two broken tags. The scan toggles on the quotes and only splits on the commas outside them.

**`src/http.c`.**

- `http_response_build_full(...)` — one implementation of the builder, with the optional `Allow` and `ETag` headers. `http_response_build` and `http_response_build_allow` are that function with the optional headers left out, so a response with an ETag cannot drift from a response without one.
- `HTTP_NO_LENGTH` — the value that says "this response has no `Content-Length` header field". It is **not** a length: a body of zero octets is `Content-Length: 0`, a claim that there is no content; a message with no length is a different claim, and the difference is a rule (§8.6 forbids the length in a 1xx or a 204; a 304 may carry one only if it equals the 200's, and this one carries none).
- The `/` route computes the tag, asks `http_if_none_match_matches`, and answers the 304 or the 200.

**No client change.** Stage 65.31 already gave the client the framing for a bodyless response; the server now produces one. The client could read the 304's head as a complete message the whole time — it just had nothing to read yet.

## 5. The tests (14)

| # | test | what it pins |
|---|---|---|
| 1 | `GET /` carries a weak ETag | the 200 has a validator, marked `W/` |
| 2 | the same document has the same ETag | the tag follows the representation, not the request |
| 3 | a different document has a different ETag | …and it is not a constant |
| 4 | a matching `If-None-Match` gets a 304 | the 304 has the ETag and the Date, no `Content-Length`, no `Content-Type`, no body |
| 5 | the tag without the `W/` still matches | the comparison is the **weak** one |
| 6 | `If-None-Match: *` gets a 304 | the asterisk matches a current representation |
| 7 | one matching tag in a list gets a 304 | the list is a list |
| 8 | a tag that does not match gets the 200 | the condition true → the method is performed |
| 9 | a value that is not an entity tag does not match | "Otherwise, the condition is true" |
| 10 | a missing target is a 404, not a 304 | §13.2.1: a non-2xx wins over a precondition |
| 11 | OPTIONS ignores the precondition | §13.2.1: OPTIONS is named |
| 12 | a HEAD with a matching precondition is a 304 | the rule covers HEAD as well as GET |
| 13 | the 304 keeps the connection | the request was framed and consumed, so the socket is still in sync |
| 14 | the client reads the ETag, and reads the 304 | the validator is visible to a client, and the 304 is a complete message |

**Three test bugs of my own, and all three were mine in the same shape.**

1. `snprintf(etag, 128, "%s", header_value)` — the header value is 256 bytes, the buffer was 128, and `-Werror=format-truncation` refused the build. A buffer that is too small for the value it is given is a bug, not a warning to suppress.
2. `strncmp(resp, "HTTP/1.1 304 Not Modified\r\n", 28)` — the literal is **27** bytes. The comparison looked at the 28th byte, which was the `D` of `Date`, and every 304 test failed for a reason that had nothing to do with the rule. This is the counted-literal trap the house rule warns about; the fix was to compare with `strlen(literal)`, not to recount.
3. The keep-alive test sent `Connection: close` on both requests, so the server hung up after the 304 and the second question was never asked. And `read_one_response` wrote its terminating NUL *at* the head's end — which ate the body's first byte, so the document the second response carried was one byte short of the document.

The lesson from all three: **a test that fails for a reason other than the rule is still a broken test.** Each one was found by reading the failure message rather than by changing the implementation.

## 6. The measurements

| | |
|---|---|
| stage binary | **14/14** |
| component total | **362** across **34** binaries (was 348/33 after Stage 65.31) |
| `make test` | exit 0 |
| `make valgrind` | exit 0, **254** clean `ERROR SUMMARY: 0 errors`, 0 leaks, 0 invalid accesses |

The count moved by exactly the new tests: 348 + 14 = 362, 33 + 1 = 34.

## 7. The discrimination proofs

Three things changed, so three proofs — each one alters *behavior*, not a header string.

| proof | what was broken | result |
|---|---|---|
| **A** | the conditional rule removed (`inm != NULL && 0`) | **8/14** — six failures, exactly the 304 assertions |
| **B** | the weak comparison replaced by a strong one (the whole tag, weakness and all) | **13/14** — one failure: "the tag without the W/ still matches" |
| **C** | the no-length mode removed (every response carries a `Content-Length`) | **13/14** — one failure: "the 304 carried a Content-Length" |

Proof B is the sharp one: the strong comparison passes thirteen of the fourteen tests, and only the test that *separates* weak from strong catches it. A rule that is only exercised by its own test is a rule with one reader.

After the proofs the source was restored (md5-verified), the tree was **rebuilt from clean**, and the suite was re-run: 362/362, stage binary 14/14. A proof's last build is the proof's binary, not the restored one.

## 8. The live oracle

**curl 8.5.0** — a client that is not us, and one that knows the rule. `curl --etag-save` stores the tag from a response; `curl --etag-compare` sends it back.

```
> GET / HTTP/1.1
> If-None-Match: W/"fnv1a64-55aea04b92853b1e"

< HTTP/1.1 304 Not Modified
< Date: Fri, 13 Nov 2026 00:08:05 GMT
< ETag: W/"fnv1a64-55aea04b92853b1e"
< Connection: keep-alive
```

The whole round trip, driven by curl: the 200 with its validator, the tag stored, the conditional request, the 304. curl did not wait for a body that never came — the same reading the RFC gives.

| request | answer |
|---|---|
| `GET /` | 200, `ETag: W/"fnv1a64-…"`, 22 bytes |
| `GET /` with the saved tag | **304**, 0 bytes |
| `GET /` with the same tag minus the `W/` | **304** — the weak comparison |
| `GET /` with `If-None-Match: *` | **304**, 0 bytes |
| `GET /` with `"other", <tag>, "another"` | **304**, 0 bytes |
| `GET /` with `"not-the-tag"` | 200, 22 bytes |
| `GET /nope` with `*` | 404 |
| `OPTIONS /` with `*` | 200, `Allow: GET, HEAD, OPTIONS` |
| `HEAD /` with the matching tag | **304** |

The 304 on the wire carries the `Date` and the `ETag` and nothing else — no `Content-Type`, no `Content-Length` — which is what "a 304 response is terminated by the end of the header section; it cannot contain content or trailers" means on the wire.

## 9. What the stage did not do, and why

- **No `If-Modified-Since`.** It is a date-based validator, and this server has no modification date to compare against. §13.1.2's date half is a separate rule with a separate input.
- **No `412`.** Unreachable by construction; §2 says why.
- **No validator on `/echo` or `/stream`.** Neither has a stable representation.
- **No client-side `If-None-Match` sender.** The client can *read* the validator (test 14) but the request builder has no slot for an extra header, so the conditional request was made with raw request bytes. The round trip is demonstrable with curl, but not yet with our own client.

## 10. What is next

**Stage 65.33 — the server's other no-content response: the 100 Continue.** The family this stage opened has three statuses — 1xx, 204, 304 — and the client half is done (Stage 65.31). The 304's server half is done (this stage). What is left is the interim one: a server that answers `Expect: 100-continue` with a 100 before the body arrives.

It is a bigger stage than this one, and the shape of why is worth writing down before it is written: the serving loop waits for the **whole** request before it does anything, so a server that must answer before the body arrives needs the parser to expose the head separately from the body — a "the head is complete" state the parser does not have today. The interim response itself is easy: the builder already has the no-length mode (this stage), and RFC 9110 §6.6.1 forbids a `Date` in a 1xx, which the builder will need to honour.

The alternatives, unchanged: the **client's conditional request** (the client can read the ETag but cannot send one back — the mirror of this stage, and the smallest of the candidates); `OPTIONS *` (a third, server-wide list — the RFC calls the asterisk request "a 'ping' or 'no-op' type of method", so the honest answer may be a 200 with no `Allow` at all); the browser's outer-loop iteration cap (a *decision* about deliberately diverging from html5lib before it needs code); the `main` rename (an OPEN question for Tom; naming-only); the deferred sockets work (TLS, `SIGPIPE`, CNAME chasing, EDNS0, TCP fallback, gzip coding, larger benchmarks, a CPU-bound handler, a warm-up, and the one-shot `http_client_get` write-loop gap); or spending the cycle on the **Vercel publish gate** — the site has been stale for twenty-seven cycles and only one toggle in Tom's account clears it.

## 11. The lessons

- **The weakness indicator is a claim about the generator.** `W/` is not decoration; §8.8.3 makes it mandatory when the tag is not collision-free. The honest tag is the one whose strength matches what the generator can promise.
- **The comparison function is chosen by the rule, not by the role.** The server did not get to prefer the strong comparison because it is simpler; §13.1.2 says MUST. And the test that proves it is the one that costs a whole proof on its own.
- **A precondition is evaluated where it can change the answer.** Two rules in §13.2.1, and both are implemented by *ordering*: the branches that must win are already ahead of the route.
- **A 304 is a claim about another message.** It carries the headers the 200 would have carried, and nothing else — no length, because the length would be a claim about content that is not there.
- **A test that fails for the wrong reason is still a broken test.** A counted literal, a buffer too small for its value, a NUL written over the body's first byte: three bugs, all mine, all found by reading the failure rather than by changing the implementation.
- **A rule with one reader is a rule with one reader.** The strong comparison passed thirteen of fourteen tests; only the test that separated the two functions caught it. A rule needs a test that can tell it apart from its neighbour.
- **The oracle is the wire, and curl can drive the whole round trip.** `--etag-save` and `--etag-compare` are not a corner case: they are the flow the RFC describes, in a client that is not us.
