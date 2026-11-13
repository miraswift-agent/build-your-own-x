# Stage 65.33 — close-out: the 100 Continue

**Branch:** `stage-65-sockets` · **Commit:** `b60e716` · **Tests:** 7 new
(component total **376** across **36** binaries) · **Valgrind:** **265 clean summaries** (0 errors / 0 leaks) across the 36 binaries.

## What shipped

RFC 9110 §10.1.1: a client may send `Expect: 100-continue` to ask the
server to confirm that it will take the body before the body is sent. Until
now our server did the second half only — it waited for the whole request and
then answered — so a client waiting for a 100 waited for nothing. That is not
a hypothetical: **curl sends `Expect: 100-continue` by itself for a body over
1024 bytes**, and waits about a second before sending the body anyway.

Three parts:

```
http_parse_status http_parser_head_next(http_parser *p, http_request *out);
int               http_parser_body_pending(http_parser *p, const http_request *r);
int               http_response_build_interim(char *dst, size_t dst_len,
                                              int status, const char *reason);
```

**The parser can say "the head is complete" on its own.** `http_parser_next()`
reported one answer for two questions — *is the head complete?* and *is the
body complete too?* — and a caller could not tell them apart. `HTTP_PARSE_NEED_MORE`
covered "head incomplete" and "head complete, body pending" alike. The new
entry point splits them: the head is parsed, the body is not, and the caller
learns which happened. **One reader for the head** — `http_parser_next()` is
now `parser_head()` followed by `parser_body()`, and both entry points go
through the same `parser_head`, so the head-level refusals of Stages
65.21–65.26 still fire from exactly one place.

**The builder can make an interim response.** `HTTP/1.1 100 Continue\r\n\r\n`
and nothing else: **no `Date`** (RFC 9110 §6.6.1 — "*A server MUST NOT generate
a Date header field in a 1xx response*"), **no `Content-Length`** (§8.6), **no
content** (RFC 9112 §6.3 item 1 — a 1xx is terminated by the empty line). Note
the **opposite** of the 304, which *keeps* the `Date` (§15.4.5): a 304 is a
final response about a representation, while a 1xx is not a response about
anything — it is the server saying "go ahead".

**The serving loop sends the 100** when three things are true at once: the
client asked (`Expect: 100-continue`), the body has not all arrived, and the
request is one we intend to serve.

## The rules, and what they say when they are silent

**§10.1.1 — the MUST.** "*A server that receives a 100-continue expectation
MUST send either a 100 (Continue) interim response or a final response.*" So
a **final response alone satisfies the rule**. That is what decides the two
cases where this server sends no 100 at all:

- **A request with no body.** There is nothing to wait for. §10.1.1 also says
  "*A client MUST NOT generate a 100-continue expectation in a request that
  does not include content*" — the expectation is meaningless here, and the
  final response satisfies the MUST.
- **A request the server refuses** (404, 405, 501, 400). The final response
  satisfies the MUST; the 100 is for a request the server intends to serve.

**§10.1.1 — the MAY.** "*A server MAY omit sending a 100 (Continue) response
if it has already received some or all of the content for the corresponding
request, or if the framing of the request makes it clear that the client will
not send content.*" That is the third case: **the body is already in the
buffer → no 100**. The server already has the content; confirming would be
theatre.

**§10.1.1 — hop-by-hop.** `Expect` is a hop-by-hop header field, so it is
never forwarded. This server forwards nothing, so the rule is satisfied by not
having a proxy — but it is the reason the expectation lives in the *request*
and not in some server-wide switch.

**§10.1.1 — the 417.** "*A server that responds with a final response to a
request that included an Expect header field with an expectation that cannot
be met by the server MUST respond with a 417 (Expectation Failed) status
code.*" **417 is deliberately not implemented here.** It is the sibling rule
— the case where the server *cannot* take the body at all — and it is a
different question from this stage's: this stage is about the interim
response, not about the refusal. What this stage does *not* do is answer a
request carrying an expectation it cannot meet; it will simply serve the
request as it would have. **An `Expect: 100-continue` on a request this
server refuses gets the refusal, not a 417** — which is right, because the
refusal is about the target, not about the body, and §10.1.1's 417 is for an
expectation the server cannot meet (a body it will not accept at all).

## The refusal is decided from the head, by the same readers

The interim response has to be decided **before** the body arrives, which means
the *refusal* has to be decided before the body arrives too — and the serving
block used to decide it after. So the decision was extracted:

```c
static int request_will_be_refused(const http_request *req, int require_host);
```

It answers *whether*, not *which status* — the status is still the serving
block's business. It calls the **same two readers** the serving block calls,
`resource_methods()` and `method_bit()`, in the same order, so the two cannot
drift apart. It is a second *call site* of the readers, not a second reader
of the rule, and the refusal is pinned by a test: the 405 case is the one the
stage's test exercises on the wire.

The 400 case (a request that does not say which site it is for, on the routed
server) is decided with the same `host_name_from_value()` the serving block
uses.

## The honest red — and its limits

**The build failed.** The test file was written first and the implementation
did not exist:

```
tests/test_stage_65_33.c:173:9: error: implicit declaration of function
  'http_parser_head_next'; did you mean 'http_parser_next'?
tests/test_stage_65_33.c:181:10: error: implicit declaration of function
  'http_parser_body_pending'
```

That is the honest red for a stage that adds an API in C, and it is worth
being precise about what it proves: it proves the test names an API that does
not exist yet. It does **not** prove the test discriminates — a red that is a
compile error says nothing about the behaviour.

**The discrimination proof is separate — and it came out differently than
expected.** With the serving-loop hook removed (the rule that sends the
100), the parser test still passes, because it does not need the hook. The
wire test does not merely fail: it **hangs**. The client is waiting for the
100 and the server is waiting for the body, and each is waiting for the
other to move first. So the proof is a timeout rather than a tally — the
test cannot pass without the hook, and it cannot even report that it failed
without it.

That deadlock is worth keeping, because it is the failure mode of the whole
feature: **a missing interim response does not fail fast, it deadlocks.**
It is also why curl sends the body anyway after about a second (RFC 9110
§10.1.1 lets a client wait a short time and then send the content whether
or not the interim response arrived). A correct server and an impatient
client can starve each other, and the cure is on the clock side.

## What the stage found in its own tests

The first green run was **6/7**, and the failure was **in the test, not in the
server**: a `strncmp` comparing 13 bytes against a 12-byte literal
(`"HTTP/1.1 405"`), so the comparison saw the space after the status and the
NUL at the end of the literal and called them different. The server had
answered `HTTP/1.1 405 Method Not Allowed` all along — the debug print showed
it. The lesson is the small one worth keeping: **a failing assertion is a
question about the test first.** The assertion was wrong, not the server.

## The lesson

**A parser that cannot say what it knows forces its caller to wait for
everything.** The parser already did the head first — the request line, the
headers, and every head-level refusal — and then did the body. It just could
not *say* so, so the serving loop waited for the whole request, and the one
response that HTTP allows between the head and the body was unreachable. The
code was not missing a feature; it was missing a **way to report a fact it
already had**.

That is the same shape as Stage 65.24's lesson on the client side ("the rule
lives in the parser, not in a route") and Stage 65.32's ("a rule with no
requester is half a rule"). Here it is a third variant: **a stage that cannot
say what it knows cannot be used where the knowledge is needed.** The interim
response was not blocked by the difficulty of sending one — it was blocked by
the parser's silence.

## What this stage does not do

- **417 Expectation Failed.** The sibling rule, left for a later stage.
- **The client's half of `Expect`** — sending the expectation, waiting for the
  interim response, and only then the body. That is the natural next stage,
  and it would make our own client the oracle instead of curl's.
- **A chunked request with an expectation.** The code path handles it (the
  decoder answers "is the body here?"), but it is not tested, and an untested
  path is not a shipped path.
- **The `Connection: upgrade` / `HTTP/2` upgrade path**, which has its own
  rules about 1xx responses.
- **Any forwarding.** `Expect` is hop-by-hop, and this server forwards
  nothing.

## Next

**Stage 65.35 — the client's** `Expect`: send the expectation, wait for the
interim response, and only then the body. The client already *reads* a 100
(Stage 65.31's tests drive `100 Continue` then `200 OK` on the wire), so the
missing half is the sending and the waiting.

Stage 65.34 — the client's *conditional* request — landed in parallel with
this stage (`5bbcab6`): it is the mirror of Stage 65.32, not a step on this
path. Two stages came out of the same tree in the same hour and the
numbering briefly ran ahead of itself: this stage was picked while 65.34
was still open, and 65.34 was picked while 65.33 was still open. The lesson
is the one this house keeps learning: **one tree, one stage at a time.** A
second writer in the same checkout does not parallelize the work, it
fragments the ledger — because the close-out, the INDEX and the master
fast-forward all have to happen in an order, and two of them cannot.
