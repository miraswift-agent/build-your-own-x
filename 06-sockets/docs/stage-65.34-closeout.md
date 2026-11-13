# Stage 65.34 — close-out: the client's conditional request

**Branch:** `stage-65-sockets` · **Commit:** `5bbcab6` · **Tests:** 7 new
(component total **369**) · **Valgrind:** **258 clean summaries** (0 errors / 0 leaks) across all
35 binaries.

## What shipped

The request builder grows **one extra-header slot**, and the client grows a
**conditional GET**. The mirror of Stage 65.32: that stage gave the *server*
the `If-None-Match` rule (a GET whose condition is false is answered with a
304), but nothing in the component could *ask* the question — the server's
half of the conditional request was a rule with no requester. This stage
gives it one.

```
http_client_request_build_with_header(dst, len, method, target, host,
                                       body, body_len, keep_alive,
                                       hdr_name, hdr_value)
http_client_get_conditional(host, port, target, etag, out)
```

`http_client_request_build_ex` is now a call to the new builder with no
extra header, so there is **one reader for the request head** and not two
(the lesson of 65.24 and 65.26: a header both sides read belongs in one
reader).

## The rules

**RFC 9110 §13.1.2 (If-None-Match).** "When an origin server receives a
request that selects a representation and that request includes an
If-None-Match header field, the origin server MUST evaluate the If-None-Match
condition per Section 13.2 prior to performing the method." The **client's**
half is the header itself, and the field is a *list* of entity tags, which
§13.1.2's evaluation reads one at a time.

**RFC 9110 §8.8.3.2 (weak comparison).** The comparison is the weak one, so
the tag the client sends back — `W/"fnv1a64-…"` — matches the tag the server
holds. The client does not have to strip the weakness indicator, and it does
not have to know that the indicator is there. That is the point of a weak
validator: *the cache stores what it was given.*

**RFC 9110 §15.4.5 (304).** The 304 carries the header fields that would
have been sent in a 200 — the `ETag` among them — and no content. The client
already knew that rule (Stage 65.31 gave it the no-body rule for 1xx/204/304);
what it did not have was a way to *ask* for one.

## The slot is checked

The extra header is not a free-form string, and the reason is not tidiness:

- **A value carrying a CR or an LF is refused** (`EINVAL`). Such a value ends
  the header line and starts another one — the request would then say two
  things about itself, and *a request that says two things about where the
  body ends is the shape request smuggling is made of* (RFC 9112 §6.3 item 3,
  the same shape Stage 65.21 refused from the other side).
- **A name carrying a colon, a space, a CR or an LF is refused.** A name
  with a colon in it is not a name; a name with a space in it is a name and
  something else.
- **An empty name or an empty value is refused.** An empty value is a header
  line with nothing in it, which is legal in HTTP but is not something to
  send by accident.
- **Exactly one of the name and the value being NULL is refused** (`EINVAL`).
  A half-given header is a mistake, and a silent one.

## The honest red, measured

**2/7.** The five that failed are the five this stage changes — the header was
not written, the three refusals were not refusals, and the conditional GET was
not conditional. The two that passed are the two that had to keep working: the
plain request must be **byte-identical** (the refactor's keep — if
`http_client_request_build_ex` had drifted while it was rewritten, this stage
would have been a change to every request the component makes), and a tag that
does not match must still get the 200 (the condition is the server's to
evaluate).

**How it was measured — and a correction worth keeping.** This stage was
written in one pass: the test file and the implementation went in together, so
there was no red run at the time it was written. The red was measured
*afterwards*, by reverting the behaviour to the pre-stage shape (the builder
ignores the extra header and does no checking; the conditional GET is a plain
GET) and re-running. That is a weaker form of the honest red than a test-first
run — it proves the tests **discriminate**, not that they were written before
the code — so it is recorded here as what it is rather than as a red that
never happened.

**The proof.** Reverting the behaviour → **2/7** (the five failures above,
exactly the five this stage changes) → restoring it → **7/7**, valgrind clean.

## The oracle is our own server

The two halves of the component now talk to each other:

1. a plain `GET /` → **200**, and the `ETag` off the answer
   (`W/"fnv1a64-…"`);
2. a conditional `GET /` with **that** tag → **304**, no body;
3. a conditional `GET /` with a tag that does not match → **200** and the
   22-byte body.

The third case matters: a conditional request that always gets a 304 would be
a client that never learns the representation changed. The condition is
evaluated by the server; the client's only job is to send the tag it was
given.

## The lessons

- **A rule with no requester is half a rule.** The server's `If-None-Match`
  handling was complete after Stage 65.32 and *unexercisable* — nothing in
  the component could produce the request that triggers it. The oracle for
  that stage had to be `curl`, because `curl` was the only thing in the room
  that could ask.
- **A header slot is a trust boundary.** The first version of the builder
  would have written any value it was given. A value with a CRLF in it is
  two header lines, and the request that carries it is a request that lies
  about its own shape.
- **One reader for the head.** The plain builder is now a call with no extra
  header, so there is no second copy of the request line, the `Host`, the
  `User-Agent` and the `Connection` to drift.
- **The mirror of a stage is not always the same size.** 65.32 (the server's
  half) needed a validator, a precondition and a no-length response mode.
  65.34 (the client's half) needed a slot, a check and one call. The mirror
  is the same *shape* — the same field, read from the other side — and not
  the same *work*.

## What this stage does not do

- **The client's half of the `Expect: 100-continue` half.** The design for
  Stage 65.33 (`docs/stage-65-33-design.md`) names it as the smaller stage
  and it is still open: it needs the client to write a head, wait for the
  interim response, and only then write the body.
- **A conditional request on any other method.** The rule is about a request
  that selects a representation; `GET` and `HEAD` are the ones this
  component's client can ask with today.
- **`If-Modified-Since`.** The other validator, and a different date
  comparison.
- **A cache.** The client can revalidate; it cannot store.

## Next

Stage 65.33 — the server's other no-content response: the **100 Continue**.
The shape is written down in `docs/stage-65-33-design.md`; the code is not
written yet. The client half of the `Expect` (send the expectation, wait for
the interim, then the body) is the smaller stage and would make our own
client the oracle instead of `curl`'s.
