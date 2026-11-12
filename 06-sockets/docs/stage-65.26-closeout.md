# Stage 65.26 — the duplicate `Content-Length` header lines — close-out

**Branch:** `stage-65-sockets` · **Code:** `cbbd014` · **Date:** 2026-11-12
**Component total:** 288 tests across 28 binaries · **Valgrind:** 0 errors / 0 leaks

---

## 1. What shipped

Stage 65.25 read *one* `Content-Length` field value and applied the list
exception to it: `"5, 5"` names one value, 5. But a message can carry the
field **more than once**, and then there is no single field value to read —
there is a **combined** one.

RFC 9110 §5.2 says how a repeated field name becomes one value:

> When a field name is repeated within a section, its combined field value
> consists of the list of corresponding field line values within that
> section, concatenated in order, with each field line value separated by a
> comma.

So two field lines

```
Content-Length: 5
Content-Length: 5
```

have the combined field value `"5, 5"` — which is exactly the list
Stage 65.25 already accepts as one value. And two field lines

```
Content-Length: 5
Content-Length: 6
```

have the combined field value `"5, 6"` — a list whose values differ, which
is an invalid framing (RFC 9112 §6.3 item 5).

**Before this stage, both parsers took the first field line and ignored the
rest.** That is a *wrong accept*: a request carrying `5` and `6` was framed
by the first, so the server read five bytes of body and treated the rest as
the beginning of the next message, while a peer that framed the message
with the other value read it differently. That is the request-smuggling
shape, and it is why RFC 9110 §8.6 says a sender "MUST NOT forward a
message with a Content-Length header field value that is known to be
incorrect".

**After this stage**, both roles read the combined field value:

| field lines | combined value | result |
|---|---|---|
| `5` | `5` | 5 — processed |
| `5` + `5` | `5, 5` | 5 — processed (one value) |
| `5` + `05` | `5, 05` | 5 — processed (the same *decimal value*) |
| `5, 5` + `5` | `5, 5, 5` | 5 — processed |
| `5` + `6` | `5, 6` | invalid → server 400 + close; user agent closes + discards |
| `5` + `abc` | `5, abc` | invalid → same |
| `5` + `6` (different case) | `5, 6` | invalid → same |

---

## 2. What the RFCs say, exactly

### RFC 9110 §5.2 — Field Lines and Combined Field Value

> When a field name is only present once in a section, the combined "field
> value" for that field consists of the corresponding field line value.
> When a field name is repeated within a section, its combined field value
> consists of the list of corresponding field line values within that
> section, concatenated in order, with each field line value separated by a
> comma.

The RFC's own example is a different field:

```
Example-Field: Foo, Bar
Example-Field: Baz
```

> The first field line has a field line value of "Foo, Bar", while the
> second field line value is "Baz". The field value for "Example-Field" is
> the list "Foo, Bar, Baz".

Note what that example shows: **the field line values keep their own
commas**. A field line value is not a single token, so the combined value
is a list whose elements are the elements of the field lines, in order.

### RFC 9112 §6.3 item 5 — the framing rule (shipped in Stage 65.25)

> If a message is received without Transfer-Encoding and with an invalid
> Content-Length header field, then the message framing is invalid and the
> recipient MUST treat it as an unrecoverable error, unless the field value
> can be successfully parsed as a comma-separated list (Section 5.6.1 of
> [HTTP]), all values in the list are valid, and all values in the list are
> the same (in which case, the message is processed with that single value
> used as the Content-Length field value). If the unrecoverable error is in
> a request message, the server MUST respond with a 400 (Bad Request)
> status code and then close the connection. […] If it is in a response
> message received by a user agent, the user agent MUST close the connection
> to the server and discard the received response.

The rule speaks of **the field value** — and §5.2 is what makes that phrase
mean one thing when the field appears twice. So the rule applies to the
**combined** value, and the two halves of the stage are the same rule read
in two roles.

### RFC 9110 §8.6 — Content-Length, the repeated-value sentence

> Likewise, a sender MUST NOT forward a message with a Content-Length
> header field value that does not match the ABNF above, with one
> exception: a recipient of a Content-Length header field value consisting
> of the same decimal value repeated as a comma-separated list (e.g,
> "Content-Length: 42, 42") MAY either reject the message as invalid or
> replace that invalid field value with a single instance of the decimal
> value, since this likely indicates that a duplicate was generated or
> combined by an upstream message processor.

**The tension, named (carried over from Stage 65.25, and this stage is
where it bites):** §8.6 gives the *recipient* a choice — reject the
message, or replace the field value with a single instance — while §6.3
item 5 says the message "is processed with that single value". The framing
section governs framing, and it says *processed*, so that is what the
component does. §8.6's "replace it with a single instance" is the same
outcome under the other name.

Also note the wording: "the same **decimal value**". That is a number, not
a spelling — so `05` and `5` are the same value, and the combined value
`"05, 5"` names one length ✓ (the test pins this).

### RFC 9110 §5.1 — the field name is case-insensitive

So `Content-Length` and `content-length` are the **same field**, and the
second line has to be read too. The test pins this: a request carrying
`Content-Length: 5` and `content-length: 6` is refused, not accepted with
the first one.

---

## 3. How the combined value is read

The combined value does not need to be materialised. RFC 9110 §5.2 says
the combined value is the field line values "concatenated in order, with
each field line value separated by a comma" — so the combined value's
**element list** is the concatenation of the field lines' element lists.
Reading the elements of each line in order *is* reading the combined
value ✓.

The reader therefore became a **scan over a list of field values**:

```c
static inline int http_content_length_scan(const char *const *values,
                                            int nvalues, long *out);
static inline int http_content_length_parse(const char *value, long *out);
static inline int http_content_length_collect(const http_header *headers,
                                               int nheaders, long *out);
```

* `http_content_length_parse` is Stage 65.25's reader, unchanged in
  behaviour (it is now a one-value wrapper over the scan ✓).
* `http_content_length_collect` is the new one: it gathers **every**
  `Content-Length` field line in the header block — matched
  case-insensitively, in order — and reads them as one combined value.

One edge case is worth writing down, because it is the only place where
"read the combined string" and "read the lines in order" could differ: an
**empty** field line. Read as a combined string, `Content-Length:` (empty)
followed by `Content-Length: 5` is `",5"` — a list with one empty element
and one value, and §5.6.1 says empty elements are skipped, so it names 5.
Read line by line, the empty line contributes no elements and the second
contributes 5 — the same answer ✓. The two readings agree because the
list rules skip empty elements, which is what Stage 65.25 already
implemented.

**The drift this closes.** Both roles had the same drift twice now:
Stage 65.25 found that the server's `strtol` accepted a sign while the
client's digit loop did not; Stage 65.26 finds that both took the first
field line while the RFC's value is the combined one. Two readers, one
header, one answer ✓ — the fix is the same both times: **one reader, in
one place, used by both roles** (`src/content_length.h`) ✓.

---

## 4. The measurements

| measurement | value |
|---|---|
| new tests | **13** |
| component total | **288** across **28** binaries |
| honest red (before the implementation) | **7/13** — six failures |
| `make test` | exit 0 |
| `make valgrind` | exit 0, **169** clean `ERROR SUMMARY` lines, 0 errors / 0 leaks |

The six honest-red failures were exactly the duplicate cases: the
server answered 200 where the RFC says 400, and the client accepted a
response it could not frame.

### Two proofs, because the stage changed two things

**Proof A — the collector takes only the first field line** (the
pre-stage behaviour, restored):

```
7/13 tests passed          6 failed
```

the same six tests that were red before the implementation ✓. The
collector is load-bearing ✓.

**Proof B — the reader refuses a repeated value** (the list exception
removed):

```
9/13 tests passed          4 failed
```

the four equal-duplicate cases failed ✓ — "two equal field lines are one
value", "the same decimal value written differently", "a list field line
and a duplicate are one list", and the client's mirror ✓. So the
equal-value acceptance is carried by the *list exception* on the combined
value, not by something incidental ✓.

Both proofs were run against a forced rebuild: **the Makefile does not
track header dependencies**, so a changed header leaves stale binaries
behind. The full gate below was run after `make clean` ✓ — and that is
worth knowing for every stage that touches a header.

---

## 5. The oracle: raw sockets on both sides, plus curl and Node

### The server, driven by raw bytes

| request | answer |
|---|---|
| one line `Content-Length: 5` | `200 OK`, `Content-Length: 5` |
| two lines `5` + `5` | `200 OK`, `Content-Length: 5` ✓ (the combined value is one value) |
| two lines `05` + `5` | `200 OK`, `Content-Length: 5` ✓ |
| two lines `5` + `6` | `400 Bad Request`, `Connection: close` ✓ |
| two lines `5` + `6`, different case | `400 Bad Request`, `Connection: close` ✓ |
| two lines `5` + `abc` | `400 Bad Request`, `Connection: close` ✓ |

The refusal's close is visible in the answer itself (`Connection: close`),
so it does not depend on the request's own `Connection` header ✓.

### curl — an independent client that sends both headers

curl really does put both headers on the wire (verified in the verbose
output, not assumed ✓):

```
> POST /echo HTTP/1.1
> Host: 127.0.0.1:18082
> User-Agent: curl/8.5.0
> Accept: */*
> Content-Length: 5
> Content-Length: 5
> Content-Type: application/x-www-form-urlencoded
```

* two equal headers → **200** ✓
* two differing headers (`5`, `6`) → **400 Bad Request** ✓
* one header → **200**, body `hello` ✓

### The client, driven by a raw peer

| response | our client | the peer |
|---|---|---|
| two equal `Content-Length: 5` lines | accepted, 200, 5 bytes ✓ | saw EOF ✓ |
| two differing `Content-Length: 5` / `6` lines | **refused**, `errno=74 (EBADMSG)`, `fd=-1` ✓ | **saw EOF** ✓ |
| one `Content-Length: 5` | accepted, 200, 5 bytes ✓ | saw EOF ✓ |

"The peer saw EOF" is the close, observed from the other side of the wire —
which is what makes it a claim about a connection and not about a
variable ✓.

### Node — the other implementation, and it differs

Node's HTTP server refuses **both** duplicate cases, with
`HPE_UNEXPECTED_CONTENT_LENGTH` and a 400 ✓ — including the equal-value
case that the framing section says should be processed.

That is not a bug in Node: RFC 9110 §8.6 explicitly gives the recipient
the choice — "MAY either reject the message as invalid or replace that
invalid field value with a single instance of the decimal value" — and
Node rejects ✓. This component follows the framing section (§6.3 item 5:
"the message is processed with that single value"), so the two
implementations answer differently on the equal-value case ✓. **When the
implementations differ, the RFC decides which is right** ✓ — and here
both are within what the RFCs allow ✓.

---

## 6. The findings

1. **A repeated field is one field.** The message carries the field twice;
   the *value* of the field is the combined list (§5.2). Reading the first
   line is not "reading the field" — it is reading half of it and
   guessing ✓.
2. **The wrong accept is the dangerous one.** A server that frames by the
   first of two differing lengths reads a body boundary the peer did not
   write — and the extra bytes become the next message ✓. That is the
   smuggling shape, and it is why §8.6's "MUST NOT forward" sentence is
   about the *sender* ✓.
3. **The same header has drifted twice.** First the *syntax* (`strtol` vs
   a digit loop), now the *multiplicity* (first line vs the combined
   value). Both times the fix was the same shape: one reader, in one
   place, used by both roles ✓. The lesson is not "be careful with
   Content-Length" — it is **a header that both sides read belongs in one
   reader** ✓.
4. **"The same decimal value" is a number.** `05` and `5` are the same
   length, so a combined value of `"05, 5"` names one value ✓. Comparing
   the text instead of the number would have been a wrong refusal ✓.
5. **The combined value does not have to exist as a string.** §5.2's rule
   is about the *element list*, and the element list of the concatenation
   is the concatenation of the element lists ✓ — so the reader reads lines
   in order and never builds a buffer ✓.
6. **A field name is case-insensitive, so a duplicate is a duplicate.**
   The second line spelled `content-length` is the same field, and
   ignoring it is the same wrong accept ✓.
7. **The Makefile does not track header dependencies.** A changed header
   leaves stale binaries behind, and a green run can be a lie ✓. Every
   stage that touches a header needs a forced rebuild before it claims a
   green suite ✓ — this one did `make clean` first ✓.
8. **The implementations differ, and that is allowed here.** Node rejects
   the equal-value duplicate; we process it ✓. §8.6 gives the recipient
   the choice, and §6.3 item 5 says the message is processed — so the
   framing section is the authority, and the difference is recorded rather
   than papered over ✓.

---

## 7. What this stage deliberately does *not* do

* **The truncated-body rule** (RFC 9112 §6.3 item 6) — still open: the
  client reports a body that ends early but leaves the connection open.
* **Closing on other parse errors** — a malformed status line desynchronises
  the stream too, but closing on every parse error is a *decision*, not a
  rule the RFC makes ✓.
* **HTTP/1.0 with a `Transfer-Encoding`** — RFC 7230 §3.3.3's MUST is
  obsolete; RFC 9112 kept the concern without the normative sentence ✓.
* **The gzip transfer coding** — still not implemented (501 on the server,
  `ENOTSUP` on the client) ✓.
* **A `Content-Length` on a HEAD response** — RFC 9110 §9.3.2's special
  rules; HEAD itself is not implemented ✓.
* **A sender-side check** — §8.6's "MUST NOT forward" is about
  intermediaries, and we are not one ✓.

---

## 8. Next

**Stage 65.27 — the truncated body** (RFC 9112 §6.3 item 6): a response
whose body ends before its `Content-Length` says it does is an invalid
framing, and the client must treat it as an unrecoverable error. This is
the last named gap in the framing family, and it is the mirror of a rule
the server already has (a request whose body is short is `NEED_MORE`
today — a connection that is still open — so the server's half needs the
"the connection closed early" case, which is exactly what the client
needs).

**What would change the pick:**

* **`HEAD` (RFC 9110 §9.3.2) and `405`** — the smaller conformance gaps,
  and a change of pace after a long run of framing rules. `HEAD` is also
  the request that makes the "no body, but a length" case interesting.
* **The browser's outer-loop iteration cap** — it needs a *decision about
  deliberately diverging from html5lib* before it needs code.
* **The `main` rename** — an OPEN question for Tom; naming only, nothing
  depends on the answer.
* **The deferred sockets work** — TLS, `SIGPIPE`, CNAME chasing, EDNS0,
  TCP fallback, larger benchmarks, a CPU-bound handler, a warm-up, and the
  one-shot `http_client_get` write-loop gap.
* **The Vercel publish gate** — the site has been stale for twenty-two
  cycles and only one toggle in Tom's account clears it.
* **Silence** does not change the pick.
