# Stage 65.25 — the Content-Length rules, in both roles — close-out

**Date:** 2026-11-12 · **Component:** `06-sockets` (C) · **Code:** `e50f481`
**Branch:** `stage-65-sockets` · **Tests:** 13 new (component total **275** across 27 binaries)

---

## 1. What shipped

The framing rules so far have all been about **transfer codings**. This
stage is about the other way a message says where its body ends: the
**length**.

Three claims, in two roles:

| claim | role | before | after |
|---|---|---|---|
| a `Content-Length` whose value is a list of **equal** values is one value | both | the server answered **400** | processed with that value |
| an unusable length in a **request** | server | 400 + close | 400 + close (pinned by a test) |
| an unusable length in a **response** | user agent | refused, **connection left open** | refused, **connection closed** |

The middle row is the one that did not change. It is in the stage
because a stage that claims a rule should be able to say which half of
it was already true.

## 2. What the RFCs say

RFC 9112 §6.3 item 5 is the whole rule:

> If a message is received without Transfer-Encoding and with an
> invalid Content-Length header field, then the message framing is
> invalid and the recipient MUST treat it as an unrecoverable error,
> **unless the field value can be successfully parsed as a
> comma-separated list (Section 5.6.1 of [HTTP]), all values in the list
> are valid, and all values in the list are the same (in which case, the
> message is processed with that single value used as the Content-Length
> field value)**. If the unrecoverable error is in a request message,
> the server MUST respond with a 400 (Bad Request) status code and then
> close the connection. … If it is in a response message received by a
> user agent, the user agent MUST close the connection to the server and
> discard the received response.

Three things are in there that are easy to miss:

1. **The field value is a LIST.** Not one number — a list of numbers,
   with exactly one legal shape: every value valid, every value the
   same. That is the exception, and the RFC writes it out because the
   common case (a duplicate header combined by an upstream processor) is
   not a number but a pair of them.
2. **The server's half and the client’s half are different claims.**
   "MUST respond with 400 and then close" versus "MUST close the
   connection and discard the response". One is about the request, one
   is about the response; neither is the other's default.
3. **The rule only fires without a Transfer-Encoding.** §6.3 item 3
   (a message with both) is checked first — that is Stage 65.21.

What a length may **look like** is RFC 9110 §8.6: `Content-Length =
1*DIGIT`. A sign is not a digit. A space is not a digit. And an empty
element in a list is not a value — RFC 9110 §5.6.1 says a recipient
must accept a list that contains empty elements, the same rule the
transfer-coding list follows (Stage 65.22).

**One tension, named.** RFC 9110 §8.6 says a recipient of a
`Content-Length` field value consisting of the same decimal value
repeated as a comma-separated list ("42, 42") **MAY** either reject the
message as invalid **or** replace that field value with a single
instance of the decimal value. RFC 9112 §6.3 item 5 says such a message
"is processed with that single value". The framing section is the one
that governs framing, and it says *processed*, so that is what this
component does — and §8.6's "replace it with a single instance" is the
same outcome under the other name.

## 3. The two roles had drifted on the same header

The server's parser read the length with `strtol`; the client's parser
read it with a digit loop. Two consequences:

- **`strtol` accepts a sign.** `Content-Length: +5` was a length of 5
  to the server and a protocol error to the client. The same header
  meant two different things on the two sides of a socket — the exact
  shape Stage 65.19's and 65.24's lessons describe: *a decoder belongs
  to the bytes rather than to the role.*
- **`strtol` also accepts trailing junk after a valid prefix?** No — but
  it accepts a leading `+`, a leading `-` (which the server then
  rejected by hand, `n < 0`), and leading whitespace.

So the reader moved to one place — `src/content_length.h`, internal to
the component — and both roles read the field value with the same code.
The two roles still differ in what they **do** with an invalid length,
because that is the role's claim to make.

## 4. The measurements

| | |
|---|---|
| new tests | **13/13** (`build/stage_65_25`) |
| component total | **275** across **27** binaries |
| full suite | `make test` → exit **0** |
| valgrind | **0 errors / 0 leaks** on all twenty-seven binaries — **158** clean `ERROR SUMMARY` lines, `VG_EXIT=0` |
| discrimination proof A | with the **list exception** removed: **9 of 13 pass, 4 FAIL** |
| discrimination proof B | with the **user agent's close** removed: **12 of 13 pass, 1 FAIL** |
| live check | a raw socket on both sides, plus curl, plus Node (§6) |

**The red run was honest and complete: 6/13.** The seven that failed
were exactly the ones the stage governs:

- the server refused `5, 5` (a wrong refusal of a request the RFC says
  to process),
- the server refused `5,` and `5, , 5` (an empty element is not a
  value),
- the server **accepted** `+5` (a sign is not a digit),
- the client refused a `5, 5` response,
- the client's reason was a protocol error rather than the framing
  reason,
- the client left the connection open.

The six that passed were the paths that had to keep working: the
differing list, the non-number, the negative length, the bad value in a
list, and the plain length on both sides.

**Two proofs, because the stage changed two things.** Proof A removes
the list exception from the shared reader: the four list tests fail
(the server and the client both go back to refusing `5, 5`), and the
other nine still pass — including the two that check a *differing* list
is refused, which do not depend on the exception. Proof B keeps the
reader and removes the close in the connection layer: exactly one test
fails — "the connection was left open" — and the reason test still
passes, because the parser still reports the framing failure. The
sources were restored and verified by md5 before the final green run:

```
f0e61377982b0035eec0e8a19689e530  src/content_length.h
3ac74cc60dd5e25dd29ac4d724b6b671  src/http_client.c
```

## 5. The three reasons, and where they travel

The parser reports **what happened** in `errno`; the role decides
**what to do** about it. Three reasons, three different claims:

| errno | meaning | who answers |
|---|---|---|
| `EPROTO` | the message is malformed (a broken request line, a header with no colon) | 400 |
| `EBADMSG` | the message's **framing** is invalid (a length that cannot be used) | 400 on the server; the user agent closes |
| `ENOTSUP` | a coding this program does not implement | 501 (Stage 65.23) |

`EBADMSG` is new in this stage. It matters because the client has to
tell *this* failure from the others: the user agent's MUST is about the
framing specifically, and a rule that closed the connection on every
parse error would be a different rule.

## 6. The oracle: a raw socket on both sides, plus curl and Node

**The server**, driven by a raw socket client (curl cannot send a
length like `5, 5` on purpose — it writes its own):

```
REQUEST  Content-Length: 5          -> HTTP/1.1 200 OK      body=b'hello'
REQUEST  Content-Length: 5, 5       -> HTTP/1.1 200 OK      body=b'hello'
REQUEST  Content-Length: 5,6        -> HTTP/1.1 200 OK      body=b'hello'
REQUEST  Content-Length: 5,         -> HTTP/1.1 200 OK      body=b'hello'
REQUEST  Content-Length: 5, , 5     -> HTTP/1.1 200 OK      body=b'hello'
REQUEST  Content-Length: 5, 6       -> HTTP/1.1 400 Bad Request
REQUEST  Content-Length: 5, abc     -> HTTP/1.1 400 Bad Request
REQUEST  Content-Length: +5         -> HTTP/1.1 400 Bad Request
REQUEST  Content-Length: -1         -> HTTP/1.1 400 Bad Request
REQUEST  Content-Length: abc        -> HTTP/1.1 400 Bad Request
```

`5` → 200 and `5, 5` → 200: the list exception, on the wire. `5, 6` →
400: the values differ. `+5` → 400: a sign is not a digit — **the
request the old server accepted.**

**The client**, driven by a raw peer that is not us:

```
OUR CLIENT: refused (errno=74 Bad message) connection_closed=yes
PEER: saw EOF -- the client closed the connection
```

and for the list exception:

```
OUR CLIENT: status=200 body_len=5 body="hello"
```

**The close is observable from the other side of the wire** — that is
what makes it a claim about a connection and not a claim about a
variable. The in-suite test asserts it the same way: the peer exits 0
only if its read ended with EOF.

**And the ordinary path still works**, against Node's own HTTP server
(an implementation that is not ours): our client read Node's chunked
response — 16 bytes, `hello from node\n`.

## 7. The findings

- **A wrong refusal is a refusal.** The server was refusing requests
  the RFC says to process. "Refusing safely" is not the same as
  "refusing correctly", and only one of them shows up in a bug report.
- **A list is not a number, and a legal list has exactly one shape:**
  every value valid, every value the same. The rule is small enough to
  fit in a header file, and the header file is where a rule belongs
  when two roles read the same bytes.
- **An empty element is not a value.** The same rule the transfer-coding
  list follows (Stage 65.22) — applied here to a different header,
  because it is a rule about **lists**, not about codings.
- **What a length may look like is `1*DIGIT`.** A sign is not a digit,
  and the two roles had drifted on that: `strtol` took `+5` and the
  digit loop did not.
- **A decoder belongs to the bytes rather than to the role.** The reader
  moved to `src/content_length.h`; the *predicate* ("what can this
  program apply") stayed with the role (65.19, 65.24).
- **The parser says what happened; the role decides what to do.** The
  parser reports `EBADMSG`; the connection layer closes, because the
  user agent's MUST is the connection layer's to keep.
- **The reason travels in `errno`** — and it survives the close, because
  the reason is the evidence of what happened.
- **A close is observable from the other side.** The peer's EOF is the
  proof; `c.fd == -1` is only the client's word about it.
- **The rule is the RFC's, not ours.** The server's half was already
  true before this stage; the stage pinned it with a test instead of
  changing it. The client's half was missing, and the RFC says MUST.

## 8. What this stage deliberately does *not* do

- **Duplicate `Content-Length` header lines.** RFC 9110 §5.2: "When a
  field name is repeated within a section, its combined field value
  consists of the list of corresponding field line values within that
  section, concatenated in order, with each field line value separated
  by a comma." Our parsers take the **first** header and ignore the
  rest, so two headers with different values are framed by the first —
  the smuggling shape, and a wrong accept. **This is the next stage**,
  and it composes with this one: two `Content-Length: 5` headers combine
  to "5, 5", which this stage's reader already accepts as one value.
- **A truncated body.** RFC 9112 §6.3 item 6: "If the sender closes
  the connection or the recipient times out before the indicated number
  of octets are received, the recipient MUST consider the message to be
  incomplete and close the connection." The client reports the
  truncation ✓ but leaves the connection open on the connection path.
- **The client does not close on other parse errors.** A malformed
  status line desynchronizes the stream just as thoroughly, and closing
  on every parse error is arguably right — but that is a **decision**,
  not a rule the RFC makes, and the stage implements the rule it can
  cite. (The one-shot `http_client_get` has no connection to keep, so
  it always closes ✓ — that is a lifecycle, not a rule.)
- **HTTP/1.0 with a `Transfer-Encoding`** — the framing is faulty and
  the connection closes. That rule is a MUST in RFC 7230 §3.3.3, which
  9112 superseded; 9112 §6.1 keeps the *concern* without the
  normative sentence.
- **The gzip coding.** Implementing it would remove the 501 and the
  refusal for `gzip, chunked` — and not for `gzip` alone. It would mean
  an inflate implementation: a project, not a stage.
- No `HEAD`, no `405`, no TLS, no `SIGPIPE`.

## 9. Next

**Stage 65.26 — the duplicate `Content-Length` header lines.** Two
headers with the same value combine (RFC 9110 §5.2) into a list that
this stage's reader already accepts; two headers with different values
combine into a list that must be refused — 400 on the server, the
connection closed on the client. The rule is already half-written; the
stage is teaching both parsers to collect every `Content-Length` line
instead of the first.

### What would change the pick

- A steer to `HEAD` (RFC 9110 §9.3.2) or `405` — the smaller HTTP
  conformance gaps, and a change of pace from a run of framing rules.
- A steer to the **truncated body** rule (§6.3 item 6) instead — a
  MUST, and the client's remaining gap in the framing family.
- A steer to the browser's **outer-loop iteration cap** (it needs a
  *decision about deliberately diverging from html5lib* before it needs
  code).
- A steer to the `main` rename — naming only; nothing depends on it.
- A steer to the deferred sockets items: TLS, `SIGPIPE`, CNAME chasing,
  EDNS0, TCP fallback, larger benchmarks, a CPU-bound handler, warm-up —
  and the one-shot `http_client_get` write loop, which still does not
  treat a zero-byte write as an error.
- A steer to the Vercel publish gate (the site has been stale for
  twenty-one cycles) instead of another stage.
- Silence does not change the pick.
