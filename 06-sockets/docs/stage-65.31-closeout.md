# Stage 65.31 close-out — the status decides: 1xx, 204 and 304 have no body

**Code:** `9d75679` — `feat(sockets): Stage 65.31 -- the status decides: 1xx, 204 and 304 have no body`
**Branch:** `stage-65-sockets` (`~/build-your-own-x`)
**Tests:** 11 new; component total **348** across **33** binaries
**Valgrind:** exit 0, **232** clean `ERROR SUMMARY: 0 errors` lines, 0 errors, 0 leaks
**Honest red:** **1/11** — the ordinary 200 was the one path that had to keep working

---

## 1. The rule, read rather than remembered

RFC 9112 §6.3 item 1 — the **first** item of the precedence list:

> Any response to a HEAD request and any response with a 1xx (Informational), 204 (No Content), or 304 (Not Modified) status code is always terminated by the first empty line after the header fields, **regardless of the header fields present in the message**, and thus cannot contain a message body or trailer section.

RFC 9112 §15.2:

> A client MUST be able to parse one or more 1xx responses received prior to a final response, even if the client does not expect one.

RFC 9110 §8.6 (the server's side, for the next stage):

> A server MUST NOT send a Content-Length header field in any response with a status code of 1xx (Informational) or 204 (No Content).

## 2. What this stage is, and what it is not

Stage 65.28 handled the **HEAD** half of item 1. The other three are different in one way that matters: **the status alone decides**.

The HEAD case needs the *conversation* — only the connection knows it asked for HEAD, the parser cannot know — so Stage 65.28 built a queue of expectations and put the flag there. A 1xx, 204 or 304 carries the answer in the status line itself, so the **parser** can see it.

Two places, two reasons. The rule does not care where the knowledge comes from, only that it is there.

**Not in this stage:** the server's half. A server that sends a 1xx or 204 MUST NOT send a `Content-Length`, and our builder always sends one — so the builder needs a no-content mode, and a route that would use it (a `304` for a conditional GET, a `100 Continue`). That is the next stage, named in §10.

## 3. The three consequences, and the three bugs

**A 204 or 304 that carries `Content-Length: 5` has no body.** The length is ignored — "regardless of the header fields present in the message" — and a client that waits for five octets waits until the peer times it out. Before this stage the client did exactly that.

**A 1xx is interim.** It is a complete message, but it is not the answer to the request; the final response follows it. Before this stage the client handed the interim to the caller *and* consumed the request in flight, so the final response was matched against the wrong expectation.

**Item 1 comes before item 5.** A 204 with `Content-Length: abc` is not invalid framing — there is no body for the length to be invalid about. And a 204 that names a coding the client cannot apply is bodyless too: there are no bytes to decode, so the `ENOTSUP` refusal of Stage 65.24 has nothing to act on.

## 4. The peer that closes after an interim

Its own case, and the reason needed its own name.

The interim response was read; the peer then hung up. The final response never came, so the request is **unanswered**. The reason is `HTTP_REASON_INCOMPLETE` — the same reason a truncated body has (Stage 65.27): the message did not arrive.

Not `EPROTO` ("there was no response"). There *was* a response, and it was not the answer. The one-shot client had the same shape of bug and the same fix.

## 5. The implementation

`src/http_client.c`:

```c
static int status_has_no_body(int status)
{
    return status == 204 || status == 304 || (status >= 100 && status < 200);
}

static int status_is_interim(int status)
{
    return status >= 100 && status < 200;
}
```

- **`parse_head`** computes `bodyless` from the status line, right after it is parsed, and uses it twice: the item-5 framing refusal (`EBADMSG`) and the unsupported-coding refusal (`ENOTSUP`) are both skipped for a message that cannot contain a body.
- **`http_response_parser_next`** has a status branch **before** the HEAD branch. The order matters: an interim response does not consume the HEAD expectation, because it does not answer the request — the final response does.
- **`http_response_parser_finish`** has the same branch: a peer that closes after a 204 has completed a message, not truncated one.
- **`http_client_connection_read_response`** asks the parser before the socket (it always did), skips an interim and keeps reading, and advances the in-flight queue **only when a final response arrives**. The queue used to advance at the start of the call — which meant an interim consumed the request it did not answer.
- **`http_client_get`** (the one-shot) reads past an interim too. Its EOF branch now asks the parser before it believes the close: the final response may already be in the buffer, fed with the interim in the same read.

`src/http_client.h`: the public contract of `http_client_connection_read_response` now states the status rule and the interim rule.

## 6. The tests (11)

`tests/test_stage_65_31.c`. The peers are raw sockets that send exactly the bytes in question — the framing is the thing under test, so the oracle has to be the wire.

| # | test | what it pins |
|---|---|---|
| 1 | a 204 with a `Content-Length` has no body | the length is not waited for |
| 2 | a 304 with a `Content-Length` has no body | the same, for the other status |
| 3 | a 1xx is interim | the final response is the answer |
| 4 | an interim does not consume the HEAD request | the HEAD expectation belongs to the final response |
| 5 | a 204 with an unusable length is complete | item 1 before item 5 |
| 6 | a 204 naming an unappliable coding is bodyless | there is nothing to decode |
| 7 | a 204 that says chunked is not chunk-decoded | the coding is not a framing here |
| 8 | a peer that closes after an interim | the reason is `HTTP_REASON_INCOMPLETE`, and the connection closes |
| 9 | an ordinary 200 still has its body | the path that had to keep working |
| 10 | a 204 leaves the connection ready for the next request | the conversation is still in sync |
| 11 | the one-shot client reads past an interim | the second implementation of the same rule |

The two-turn peer carries **its own deadline** (`SO_RCVTIMEO`): a client that waits for a body that will never come must not take the whole suite down with it. The peer gives up and closes; the client sees the close and the test fails with a reason. (The house lesson from Stage 65.22: *a hang is a poor failure mode, and the suite's alarm is a poor place to discover it*.)

## 7. The measurements

| measurement | question it answers | result |
|---|---|---|
| honest red | what did not work before the stage? | **1/11** — ten failures, and the one that passed was the ordinary 200 |
| `make clean && make test` | does the whole component still pass from a forced rebuild? | exit 0 — **348/348** across **33** binaries |
| `make valgrind` | is the new code clean under the checker? | exit 0 — **232** clean summaries, 0 errors, 0 leaks |
| the stage binary, directly | does the shipped stage still pass after every proof? | **11/11** |

The Makefile does not track header dependencies, so the full gate ran after `make clean` — and `src/http_client.h` changed in this stage.

## 8. The discrimination proofs

**Proof A — the status rule removed** (`status_has_no_body` returns 0):

```
1/11 tests passed
10 FAILED
```

The ten failures are exactly the ten the stage changes; the ordinary 200 still passes. The rule is load-bearing.

**Proof B — the interim skip removed** (`status_is_interim` returns 0, both the connection and the one-shot):

```
7/11 tests passed
4 FAILED
```

The four failures are the interim tests — and **the status-rule tests still pass**, because the two rules are independent: one is about how a message is framed, the other about which message answers.

Both proofs: the source was restored and md5-verified (`e698c6e2febfc7b18c5f35f02046eae6`), and the binary was rebuilt after the restore — *a proof's last build is the proof's binary, not the restored one* (Stage 65.29's lesson).

## 9. The live oracle

**curl**, against a peer that sends exactly the bytes in question:

| peer sends | curl says | our client says |
|---|---|---|
| `HTTP/1.1 204 No Content` + `Content-Length: 5` | `http_code=204 size=0` | `status=204 body_len=0 content_length=5` |
| `HTTP/1.1 100 Continue` then `HTTP/1.1 200 OK` + 2 bytes | `http_code=200 size=2` | `status=200 body_len=2` |

Two independent implementations, the same answers. Two details worth naming:

- curl **did not wait** for the five octets the 204 promised — the same reading the RFC gives.
- our client **reports** `content_length=5` while taking no body: the length is a claim, and a claim is worth reporting even when the bytes are not coming. (The HEAD case established this in Stage 65.28: the length describes the body the matching GET would have returned.)

## 10. What is next

**Stage 65.32 — the server's side: a response with no content.** The builder needs a mode that writes neither a `Content-Type` nor a `Content-Length` (RFC 9110 §8.6 forbids the length in a 1xx or 204), and a route that would use it:

- a **304** for a conditional GET — RFC 9110 §15.4.5: a 304 "MUST generate any of the following header fields that would have been sent in a 200 (OK) response" (`Content-Location`, `Date`, `ETag`, `Vary`), and "is terminated by the end of the header section; it cannot contain content or trailers". An `ETag` needs a validator, and a weak one (`W/"..."`) is the honest choice unless the hash is collision-free.
- a **100 Continue** — RFC 9112 §10.1.1: the server may send it before the body arrives, which means the serving loop needs a hook where it currently waits for the whole request.

The alternatives, unchanged: `OPTIONS *` (a third, server-wide list — the RFC calls the asterisk request "a 'ping' or 'no-op' type of method", so the honest answer may be a 200 with no `Allow` at all); the browser's outer-loop iteration cap (a *decision* about deliberately diverging from html5lib before it needs code); the `main` rename (an OPEN question for Tom; naming-only); the deferred sockets work (TLS, `SIGPIPE`, CNAME chasing, EDNS0, TCP fallback, gzip coding, larger benchmarks, a CPU-bound handler, a warm-up, and the one-shot `http_client_get` write-loop gap); or spending the cycle on the **Vercel publish gate** — the site has been stale for twenty-six cycles and only one toggle in Tom's account clears it.

## 11. The lessons

- **The knowledge lives with whoever can know it.** Stage 65.28 put the HEAD expectation in the connection (only it knew the method); Stage 65.31 put the status rule in the parser (the status line is right there). Two places, two reasons — the rule does not care where the knowledge comes from.
- **"Regardless of the header fields present in the message" is the load-bearing half of item 1.** It is what makes a 204 with a length bodyless, and what makes a 204 that says chunked undecoded.
- **An interim response is a complete message that is not the answer.** The distinction is why the request stays in flight and why the HEAD expectation belongs to the final response.
- **Advancing the queue at the start of a read was a bug waiting for an interim.** The queue moved to the moment the final response arrives.
- **"There was no response" and "there was a response that was not the answer" are different failures** — `EPROTO` versus `HTTP_REASON_INCOMPLETE`. The reason is the evidence.
- **A test's peer should carry its own deadline.** The suite's alarm is the last resort, not the first detector.
- **The oracle is the wire.** Two independent implementations reading the same bytes and giving the same answers is worth more than either implementation alone.
