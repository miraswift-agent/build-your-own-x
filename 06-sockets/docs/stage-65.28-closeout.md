# Stage 65.28 — the response to `HEAD` — close-out

**Branch:** `stage-65-sockets` · **Code:** `786aca7` · **Date:** 2026-11-12
**Component total:** 311 tests across 30 binaries · **Valgrind:** 0 errors / 0 leaks

---

## 1. What shipped

A response to a `HEAD` request carries the head the matching `GET` would
have carried, and **no content**.

**Before this stage, neither half existed.** The server answered a `HEAD`
request with the body a `GET` would have returned, and the client waited
for a body the server never sent — which, on a keep-alive connection, is
a client that sits there until the peer times it out.

**After this stage:**

| | before | after |
|---|---|---|
| server, `HEAD /` | 200 + `Content-Length: 22` + 22 body bytes | 200 + `Content-Length: 22` + **no body bytes** |
| server, `HEAD /stream` | the chunked head **and the chunks** | the chunked head alone |
| server, `HEAD /echo` (with a body) | the echoed body | `Content-Length: 0`, no body |
| client, a response to `HEAD` | waits for the promised octets | returns at the end of the head, `body_len == 0` |
| client, `Content-Length` on a `HEAD` response | refused as invalid framing | accepted; the length is reported, the body is empty |

---

## 2. What the RFCs say, exactly

The text was fetched from `rfc-editor.org` (`rfc9110.txt`, `rfc9112.txt`)
rather than quoted from memory — the citation is part of the claim, and
the house has been bitten by that rule before.

### RFC 9110 §9.3.2 — HEAD

> The HEAD method is identical to GET except that the server MUST NOT
> send content in the response.

> The server SHOULD send the same header fields in response to a HEAD
> request as it would have sent if the request method had been GET.

> Although request message framing is independent of the method used,
> content received in a HEAD request has no generally defined semantics,
> cannot alter the meaning or target of the request, and might lead some
> implementations to reject the request and close the connection because
> of its potential as a request smuggling attack.

### RFC 9112 §6.3 item 1 — the first item of the precedence list

> Any response to a HEAD request and any response with a 1xx
> (Informational), 204 (No Content), or 304 (Not Modified) status code is
> always terminated by the first empty line after the header fields,
> regardless of the header fields present in the message, and thus cannot
> contain a message body or trailer section.

Two words in that sentence carry the stage: **first** (it is item 1 of a
list that is explicitly "in order of precedence") and **regardless** (the
header fields do not matter).

### RFC 9110 §8.6 — Content-Length

> A server MAY send a Content-Length header field in a response to a HEAD
> request (Section 9.3.2); a server MUST NOT send Content-Length in such a
> response unless its field value equals the decimal number of octets that
> would have been sent in the content of a response if the same request had
> used the GET method.

So the `Content-Length` in a `HEAD` response is **a claim about another
message** — the `GET`'s body — not a promise about this one. The client
that waits for those octets is waiting for bytes that were never coming.

### RFC 9110 §9.1 — the method token is case-sensitive

> The method token is case-sensitive because it might be used as a
> gateway to object-based systems with case-sensitive method names. By
> convention, standardized methods are defined in all-uppercase US-ASCII
> letters.

So `head` is not `HEAD`. The server compares the method exactly, and a
request for `head /` may carry a body.

---

## 3. The server's half: build the `GET`'s response, send only its head

The server already builds the response it would send for a `GET` — status
line, `Date`, `Content-Type`, `Content-Length`, `Connection`, the body.
For a `HEAD` request it builds **that same response** and then sends only
the bytes up to and including the empty line that ends the header
section:

```c
static size_t response_bytes_to_send(const char *resp, size_t n, int is_head)
```

That is the literal shape of §9.3.2: the response to a `HEAD` is the
response to the corresponding `GET` **except that it does not include
content** — so the head is the `GET`'s head, byte for byte, and the
content is left off the wire.

The live measurement: `curl -I` against our server returns **125 bytes**,
and the first 125 bytes of the `GET` response (`curl -i`) are **identical
to it** (`cmp` says so). The `GET`'s response is those 125 bytes plus the
22 body bytes.

**The `Content-Length` is therefore the `GET`'s length, for free** — the
head was built for the `GET`, so the length in it is the length the
`GET`'s body had ✓ (RFC 9110 §8.6's MUST is satisfied by construction,
not by a special case ✓).

### The one place the rule needed a decision: `/echo`

`/echo` answers with the request's body, so "the response the matching
`GET` would have produced" needs a reading. The RFC's own words decide
it: *"content received in a HEAD request has no generally defined
semantics"* (§9.3.2) — so the request's content does not enter the
response, and the matching `GET` (which carries no content) would answer
with an empty body. The `HEAD` response therefore says
`Content-Length: 0` ✓.

The other reading — "the same request, with the method changed to `GET`,
body and all" — is defensible on its own terms, and the component's
choice is recorded here rather than buried: **the content of a `HEAD`
request has no defined semantics, so the response does not depend on it.**

Note what the server still does: it **reads** the request's body. The
framing of a request is independent of the method (§9.3.2 says so
explicitly), so the bytes are consumed and the connection stays in
sync ✓ — a `HEAD` request with a body is not a smuggling hole, because
the body is framed and consumed like any other ✓.

### `/stream`: the head alone is the whole response

The chunked route's head says `Transfer-Encoding: chunked` and carries no
length ✓. For a `HEAD` request the head goes out and the chunks do not
✓ — and that is not a truncated body: §6.3 item 1 terminates the
response at the empty line *regardless of the header fields present* ✓,
so a head that names a coding, with no coding applied, is a complete
response ✓.

---

## 4. The client's half: know which response has no body

The client is the end of the chain, so it cannot pass a body-less
response along — it has to *read* it. RFC 9110 §9.3.2:

> A client SHOULD NOT interpret the response to a HEAD request as
> containing content, since the response always terminates at the end of
> the header section, whether or not the response is an error.

So the parser needs to know that the response it is reading answers a
`HEAD` request. **The connection knows** — it wrote the request — so the
knowledge travels with the conversation:

```c
int in_flight[HTTP_CLIENT_MAX_INFLIGHT];   /* 1 = a HEAD response, no body */
int n_inflight;
```

**A queue, not a flag.** Two requests can be in flight at once (Stage
65.15b's pipelining), and the responses come back in the order the
requests were written — so the expectations are a queue, and the front of
the queue is the response being read ✓. A single flag would be wrong the
moment a `HEAD` and a `GET` were pipelined, and "wrong" here is the
kind of wrong that hangs ✓.

The parser's new flag is `no_body`, and it is consumed when a response
completes ✓ — not when the head is parsed, because a head that is not
complete yet is not a response yet ✓.

### What the client does with the body it is not given

* `body_len` is 0 ✓ — there is no body.
* `content_length` still reports the header's value ✓ — it is a fact
  about the header, and a fact about the `GET`'s body ✓. A caller can
  use it to decide whether to fetch the representation ✓ — which is the
  entire point of `HEAD` ✓.
* The bytes that follow the head, if any, are **kept** as the start of
  the next message ✓ — a server that sent content anyway broke the rule,
  and the client does not take those bytes as a body ✓ (§9.3.2's
  "SHOULD NOT interpret the response to a HEAD request as containing
  content" ✓).

### Item 1 comes before item 5: the precedence order is part of the rule

A `HEAD` response whose `Content-Length` is `abc` — a length that cannot
be used — is **still complete**. §6.3 is a list "in order of precedence",
and item 1 comes before item 5 ✓: a response to `HEAD` is terminated by
the end of its head *regardless of the header fields present in the
message* ✓. There is no body for a bad length to be invalid about ✓.

So the length validation is skipped for a response that has no body, and
the length is reported as unknown (-1) when it cannot be read ✓. A rule
that is only half-read becomes the wrong rule: reading item 5 without
item 1 would refuse a message that item 1 says is complete ✓.

### The peer closing after a `HEAD` response

`http_response_parser_finish` also learned the rule: a peer that hangs up
after a `HEAD` response has **completed** its message ✓ — the response
ended at the end of its head ✓, and the close is the end of a
conversation, not a message that stopped early ✓.

---

## 5. The measurements

| measurement | value |
|---|---|
| new tests | **11** |
| component total | **311** across **30** binaries |
| honest red (before the implementation) | **1/11** — ten failures |
| `make test` | exit 0 |
| `make valgrind` | exit 0, **191** clean `ERROR SUMMARY` lines, 0 errors / 0 leaks |

The honest red was **1/11**: the ten failures were the ten tests the
stage changes, and the one that passed was the narrowness test — a
lowercase `head` is not `HEAD`, so it may carry a body ✓.

### Two proofs, because the stage changed two things

**Proof A — the server ignores the method** (`is_head = 0`):

```
6/11 tests passed          5 failed
```

the five server-side tests ✓ — the content was sent after the head ✓,
the chunks were sent ✓, the echo came back ✓.

**Proof B — the client does not know the response is to a `HEAD`**
(`parser.no_body = 0`):

```
6/11 tests passed          5 failed
```

the five client-side tests ✓ — the client waited for the body ✓, took
the content as the body ✓, refused the unusable length ✓, waited for
the terminal chunk ✓, and timed out against our own server ✓.

Both proofs ran against a forced rebuild: **the Makefile does not track
header dependencies** ✓ (this stage changed a header ✓), so the full
gate ran after `make clean` ✓.

---

## 6. The oracle: curl, Node, raw sockets, and our own client

### Our server, through curl

```
$ curl -sS -I --http1.1 http://127.0.0.1:18083/
HTTP/1.1 200 OK
Date: Thu, 12 Nov 2026 21:52:56 GMT
Content-Type: text/html
Content-Length: 22
Connection: keep-alive
```

125 bytes, and `cmp` says the first 125 bytes of the `GET` response are
**identical** to them ✓ — the `HEAD` response *is* the `GET`'s head ✓.

`HEAD /stream` → the chunked head and no chunks ✓. `HEAD /nope` → the
404 head with `Content-Length: 14` and no body ✓.

### Our server, through a raw socket

`HEAD /echo` with `Content-Length: 5` and the body `hello` on the wire:

```
HTTP/1.1 200 OK\r\nDate: ...\r\nContent-Type: application/octet-stream\r\nContent-Length: 0\r\nConnection: close\r\n\r\n
```

head only ✓, the request's content not echoed ✓, and the length is the
matching `GET`'s ✓.

A lowercase `head /` → the response **carries the body** ✓ — the method
token is case-sensitive ✓.

### Node, as the other implementation

Node's server answers a `HEAD` with `Content-Length: 5` and no body ✓,
and **Node's own client** reads it as `status=200, body_bytes=0,
content-length=5` ✓ — an independent implementation agreeing that the
length is a fact and the body is not there ✓.

### Our client

| against | result |
|---|---|
| Node's `HEAD` response | `status=200 body_len=0 content_length=5` ✓ (and it did not hang ✓) |
| Node's `GET` response | `status=200 body_len=5 content_length=5` ✓ (unchanged ✓) |
| our server's `HEAD` response | `status=200 body_len=0 content_length=22` ✓ |
| our server's `GET` response | `status=200 body_len=22 content_length=22` ✓ |

---

## 7. The findings

1. **A framing rule can be about a body that is not there.** Every rule
   in the last six stages said *where the body ends*. This one says the
   body does not begin — and it is the same machinery: the head is the
   boundary ✓.
2. **The `Content-Length` of a `HEAD` response is a claim about another
   message.** It describes the `GET`'s body ✓. A client that treats it
   as a promise about *this* response waits for bytes that are never
   coming ✓ — and a length is only a promise when the message actually
   has one ✓.
3. **The precedence list is a list.** Item 1 (no body) comes before
   item 5 (a bad length) ✓, so a `HEAD` response with an unusable length
   is complete ✓. Reading item 5 without item 1 would refuse a message
   the RFC says is complete ✓ — a rule that is only half-read becomes
   the wrong rule ✓.
4. **Build the `GET`'s response and leave the content off the wire.**
   The head — including the length — is then correct by construction ✓
   rather than by a special case per route ✓.
5. **The knowledge lives with the conversation, not with the bytes.**
   The parser cannot know whether a response has a body; the connection
   can, because it wrote the request ✓. And with pipelining, "can" means
   a **queue** ✓ — a single flag is wrong the moment two requests are in
   flight ✓, and wrong here means a hang ✓.
6. **A method token is case-sensitive.** `head` is not `HEAD` ✓, and a
   server that treats them alike answers a request with a body it should
   not ✓.
7. **The content of a `HEAD` request has no defined semantics** — so it
   does not enter the response ✓, while the *framing* of the request
   still does ✓ (the body is read and consumed ✓, so the connection
   stays in sync ✓).
8. **A peer that hangs up after a `HEAD` response has finished its
   message** ✓ — the close is the end of a conversation, not a message
   that stopped early ✓. The same close means two different things for
   two different messages ✓, and the parser has to know which ✓.

---

## 8. What this stage deliberately does *not* do

* **304, 204, and 1xx responses** — the other three cases of §6.3 item
  1. They are one line each in the same place (a response with no
  content), but our server never sends them, so there is nothing to
  learn from them yet ✓.
* **`405 Method Not Allowed`** — still open, and the smaller conformance
  gap ✓.
* **The other methods** — `POST`, `PUT`, `DELETE` are all answered by
  the same routes as `GET` (the server does not dispatch on the method
  except for `HEAD` now) ✓.
* **The truncated-body rule** — closed in Stage 65.27 ✓.
* **The `Content-Length` of a `HEAD` response when the server omits it**
  — §8.6 says MAY ✓, and ours sends it ✓ because the head was built for
  the `GET` ✓.

---

## 9. Next

**Stage 65.29 — `405 Method Not Allowed`** (RFC 9110 §15.5.6): the
server answers a method it does not implement for a resource with `405`
and an `Allow` header naming the methods it does implement ✓. It is the
smallest remaining conformance gap ✓, and it completes the picture this
stage opened: **a request has a method, and the method says what the
response is** ✓ — `HEAD` gets the head without the content ✓, and an
unknown method gets a refusal that says which methods are allowed ✓.

**What would change the pick:**

* **`304`, `204`, and `1xx`** — the other three cases of §6.3 item 1,
  and the same place in the code as this stage ✓.
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
