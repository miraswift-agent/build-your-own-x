# Stage 65.27 — the truncated body — close-out

**Branch:** `stage-65-sockets` · **Code:** `ccde568` · **Date:** 2026-11-12
**Component total:** 300 tests across 29 binaries · **Valgrind:** 0 errors / 0 leaks

---

## 1. What shipped

A response can promise a body and not deliver it. `Content-Length: 10`
says ten octets follow; three arrive and the peer closes. That is not a
short response — it is an **incomplete** one.

**RFC 9112 §6.3 item 6** is the rule:

> If a valid Content-Length header field is present without
> Transfer-Encoding, its decimal value defines the expected message body
> length in octets. If the sender closes the connection or the recipient
> times out before the indicated number of octets are received, the
> recipient MUST consider the message to be incomplete and close the
> connection.

**RFC 9112 §8** gives the same rule its vocabulary, and says what
"incomplete" means:

> A client that receives an incomplete response message, which can occur
> when a connection is closed prematurely or when decoding a supposedly
> chunked transfer coding fails, MUST record the message as incomplete.

> A message body that uses the chunked transfer coding is incomplete if
> the zero-sized chunk that terminates the encoding has not been
> received. A message that uses a valid Content-Length is incomplete if
> the size of the message body received (in octets) is less than the
> value given by Content-Length. A response that has neither chunked
> transfer coding nor Content-Length is terminated by closure of the
> connection and, if the header section was received intact, is
> considered complete.

**Before this stage** the client *detected* the truncation
(`http_response_parser_finish` reported an error) but reported it as
`EPROTO` — a malformed message — and **left the connection open**. The
MUST is the close, and the word is "incomplete", which is neither
malformed nor invalid framing.

**After this stage**:

* the parser reports the condition with its own reason,
  `HTTP_REASON_INCOMPLETE` ✓;
* the client **closes the connection** when the message is incomplete ✓
  (the §6.3 item 6 MUST ✓), and the reason survives the close ✓;
* the one-shot client (`http_client_get`) reports the same reason
  instead of overwriting it with `EPROTO` ✓;
* a response whose **head** stops is incomplete too ✓ (the client cannot
  assume the status line means what it seems to mean ✓ — §8 ✓);
* a peer that closed **without sending anything** is still "there was no
  response" (`EPROTO`) ✓ — not an incomplete one ✓;
* a response with **no framing** is still **complete** when the peer
  closes ✓ — the rule is narrow ✓;
* a **malformed** response is still not an incomplete one, and the
  client still does **not** close for it ✓ (that boundary is a decision,
  not an RFC MUST ✓).

The **server's half** was already right by construction: the request
parser waits for the whole body (`NEED_MORE`, not an error), so an
incomplete request never becomes a request and is never served. This
stage **pins** that with a test rather than changing it.

---

## 2. The reason, and why it is not an errno

The house already had three reasons, and each one names what happened:

| reason | means | role's answer |
|---|---|---|
| `EPROTO` | the message is malformed | 400 (server) / no close (client) |
| `EBADMSG` | the framing is invalid | 400 + close (server) / close + discard (client) |
| `ENOTSUP` | a coding we cannot apply | 501 (server) / refuse (client) |

A fourth one is needed, and it is not one of the three: **the framing
was fine and the bytes did not arrive.** The length was valid; the
octets were missing. That is neither a malformed message nor an invalid
framing — it is an **incomplete** one.

So the component names it itself:

```c
#define HTTP_REASON_INCOMPLETE 1001
```

in `src/http.h`, deliberately **outside the system errno range**. The
errno space has no value for "the message did not arrive", and
borrowing one would claim something about the wire that is not true:
`EPIPE` is a write to a closed pipe, `ECONNRESET` is an aborted
connection, `ETIMEDOUT` is a clock — and the peer here closed
**cleanly**. A FIN is not a reset, and naming it as one would put a
false claim in the reason that travels with the failure. The value sits
in a range no system errno uses.

---

## 3. What the stage found

The rule was already half-written, the way Stage 65.19 was half-written
when it found a decoder that existed. The detection was there:

* `http_response_parser_finish` already refused a body that stopped ✓;
* it already refused a chunked body that stopped before its terminal
  chunk ✓.

What was missing was the **three** things the RFC asks for:

1. **the word** — the failure was reported as `EPROTO`, a malformed
   message ✓, so the reason said the wrong thing ✓;
2. **the close** — the connection was left open ✓, so the §6.3 item 6
   MUST was not met ✓;
3. **the survival** — the one-shot path overwrote the reason with
   `EPROTO` on its way out ✓, so the evidence was lost ✓.

And the **narrowness** was not written down anywhere: a response with
no framing is *complete* when the peer closes (§8 says so ✓), and a
malformed response is not an incomplete one ✓. Both are now pinned by
tests ✓, because the tempting implementation — "close on every
failure" — would have been wrong in one direction and the RFC's
distinctions are the whole content of the rule.

---

## 4. The measurements

| measurement | value |
|---|---|
| new tests | **12** |
| component total | **300** across **29** binaries |
| honest red (before the implementation) | **6/12** — six failures |
| `make test` | exit 0 |
| `make valgrind` | exit 0, **179** clean `ERROR SUMMARY` lines, 0 errors / 0 leaks |

The six honest-red failures were exactly the six cases the stage
changes: the client's refusal-and-close, the really-closed descriptor,
the chunked body that stops, the parser's reason, the head that stops,
and the one-shot path's reason. The six that must keep working already
passed: a close-delimited response is complete, a complete response is
read, "no response at all" is still `EPROTO`, the server does not serve
an incomplete request, the server does not serve a chunked request that
stops, and a malformed response does not close the connection.

### Three proofs, because the stage changed three things

**Proof A — the reason reverted to `EPROTO`:**

```
6/12 tests passed          6 failed
```

The same six ✓. The reason is load-bearing ✓ — and because the close
is keyed on the reason, the revert also removes the close ✓, which is
the honest result of that experiment.

**Proof B — the close removed, the reason kept:**

```
9/12 tests passed          3 failed
```

the three close assertions ✓. The MUST is carried by the close, not by
the reason ✓.

**Proof C — the one-shot `errno` overwrite restored:**

```
11/12 tests passed         1 failed
```

the one-shot path's reason ✓. The evidence survives the path that
closes the socket ✓.

All three were run against a forced rebuild: **the Makefile does not
track header dependencies**, and this stage changed a header ✓ — so the
full gate below ran after `make clean` ✓.

---

## 5. The oracle: a live peer, and Node's own client

A Node TCP peer that promises ten octets, sends three, and closes
cleanly (a FIN, not an RST):

```
LIVE: refused, errno=1001 (HTTP_REASON_INCOMPLETE) fd=-1
LIVE: incomplete, and the connection was closed
```

**Node's own HTTP client, against the same peer:**

```
NODE: error: ECONNRESET aborted
```

Node also refuses to deliver the truncated response ✓ — an independent
implementation agreeing that the message is not delivered ✓. Node names
the condition at the socket level (`ECONNRESET` ✓); this component names
it at the message level (`HTTP_REASON_INCOMPLETE` ✓). Both are right
about the same fact, and the difference is only in how each library
names it ✓.

**And the narrowness, live:** a peer whose response has no framing at
all — the body ends when the peer closes:

```
LIVE2: accepted, status=200 body_len=5 body=hello
LIVE2: a close-delimited response is complete
```

That is §8's last sentence, checked against a live peer ✓: the rule
fires on a body that was promised and did not arrive, **not** on a
close ✓.

The server's half was checked on the wire too, in the test binary: a
request whose body is short (and a chunked request whose terminal chunk
never arrives) get **no answer at all** ✓ — the server does not serve a
request whose body never arrived ✓, and it closes the connection ✓.
§8 says the server "MAY send an error response prior to closing the
connection" ✓ — MAY, and the peer is gone ✓, so ours closes without
answering ✓.

---

## 6. The findings

1. **A promise is a claim about bytes, and a claim can go unpaid.**
   `Content-Length: 10` is not a hint; it is the boundary of the
   message, and a message that stops before its boundary is not a
   smaller message ✓.
2. **Detection is not compliance.** The client already *knew* the body
   had stopped ✓ — and still left the connection open ✓. The rule has
   two parts (record it, close the connection ✓) and the first one was
   already true ✓.
3. **"Incomplete" is a third kind of failure**, beside malformed ✓ and
   invalidly framed ✓ — and a reason that names the wrong kind makes
   the role's decision wrong ✓: the malformed path does not close ✓,
   and this one must ✓.
4. **The close is the claim.** The rule's consequence is a wire
   action ✓, and it is observable: the descriptor is gone ✓ (a read on
   the saved fd gives `EBADF` ✓ — not a field set to `-1` ✓).
5. **A rule that is only half-read becomes the wrong rule.** "The peer
   closed" is not the trigger ✓; "a promised body did not arrive" is ✓.
   A close-delimited response ends at a close ✓ and is complete ✓.
6. **A FIN is not a reset.** The reason must not claim something the
   wire did not do ✓ — which is why the reason is the component's own
   and not a borrowed errno ✓.
7. **The evidence has to survive the close.** The one-shot path
   overwrote the reason on its way out ✓, so a caller could not tell
   why the request failed ✓. The reason is the evidence ✓.
8. **The server's half was already right** — and the way to know is a
   test, not an assumption ✓. "The parser waits for the whole body"
   means an incomplete request never becomes a request ✓, so it is
   never served ✓.
9. **The Makefile does not track header dependencies** ✓ — and this
   stage changed a header ✓.

---

## 7. What this stage deliberately does *not* do

* **The timeout half of item 6** — "or the recipient times out" ✓ —
  the component has no timeout support ✓. Named gap ✓.
* **The server's MAY** — an error response before closing an
  incomplete request ✓ (RFC 9112 §8 ✓). The peer is gone in the case
  we handle ✓, and a peer that *waits* for a body it never sends is
  the timeout case ✓, which we do not have ✓.
* **Closing on other parse errors** — a malformed status line
  desynchronises the stream too ✓, but closing on every failure is a
  *decision* ✓, not an RFC MUST ✓. The test pins the boundary ✓.
* **HTTP/1.0 with a `Transfer-Encoding`** — RFC 7230 §3.3.3's MUST is
  obsolete ✓; RFC 9112 keeps the concern without the normative
  sentence ✓.
* **The gzip transfer coding** ✓, **TLS** ✓, **`SIGPIPE`** ✓, and the
  rest of the deferred sockets list ✓.
* **Caching an incomplete response** — RFC 9112 §8 points at
  RFC 9111 §3.3 for the cache requirements ✓; this component has no
  cache ✓.

---

## 8. Next

The framing family is now **complete**: every named gap it had is
closed — the coding list ✓ (65.22–65.24 ✓), the length ✓ (65.25 ✓),
the repeated length ✓ (65.26 ✓), and the body that stops ✓ (this
stage ✓).

**Stage 65.28 — the response to `HEAD`** (RFC 9110 §9.3.2 ✓, with RFC
9112 §6.3 item 1 ✓): a response to a HEAD request "is always terminated
by the first empty line after the header fields, regardless of the
header fields present in the message" ✓ — so the server answers a HEAD
request with the head it would have sent and **no body** ✓, and the
client must not wait for a body that will never come ✓. That is the
same shape as this stage from the other side: **a framing rule that is
about a body that is not there** ✓ — and `HEAD` is also the request
that makes "a length without a body" interesting ✓, because the
`Content-Length` of a HEAD response describes the body the *matching*
GET would have returned ✓.

**What would change the pick:**

* **`405` (Method Not Allowed)** — the smaller conformance gap, and a
  change of pace after a long run of framing rules ✓.
* **The browser's outer-loop iteration cap** — it needs a *decision
  about deliberately diverging from html5lib* before it needs code ✓.
* **The `main` rename** — an OPEN question for Tom ✓; naming only ✓.
* **The deferred sockets work** — TLS ✓, `SIGPIPE` ✓, CNAME chasing ✓,
  EDNS0 ✓, TCP fallback ✓, larger benchmarks ✓, a CPU-bound handler ✓,
  a warm-up ✓, and the one-shot `http_client_get` write-loop gap ✓.
* **The Vercel publish gate** — the site has been stale for
  twenty-three cycles ✓ and only one toggle in Tom's account clears it
  ✓.
* **Silence** does not change the pick ✓.
