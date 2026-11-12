# Stage 65.23 — the server answers 501 to a request whose transfer coding it does not understand — close-out

**Date:** 2026-11-12 · **Component:** `06-sockets` (C) · **Code:** `736d250`
**Branch:** `stage-65-sockets` · **Tests:** 9 new (component total **251** across 25 binaries)

---

## 1. What shipped

RFC 9112 §6.1 says one thing about a server and a coding it does not
know:

> A server that receives a request message with a transfer coding it
> does not understand SHOULD respond with 501 (Not Implemented).

The coding header is a **list**, and each coding in it is applied to the
body in turn. `Transfer-Encoding: gzip, chunked` means: the content was
compressed, and then the compressed content was chunked.

We know how to read the chunked coding — that is what Stage 65.11's
decoder is for. We do not know how to decompress gzip. So until this
stage, the server framed the body correctly, took the chunks apart, and
handed back the **still-gzip-coded bytes** with a 200.

That is the failure this stage closes. A body that is still compressed,
returned as a 200, is a server claiming to have processed a message it
did not understand. The bytes are the right bytes and the answer is the
wrong answer: the client asked for a document and got a compressed
stream wearing the document's name.

So this stage ships:

```
POST /echo  (Transfer-Encoding: gzip, chunked)  ->  501 Not Implemented, then close
```

## 2. 400 says "this request is wrong"; 501 says "this server cannot"

The two refusals before this one both answer 400, and this one answers
501. That difference is not cosmetic — it is a claim about **who** is
at fault:

| status | the claim |
|---|---|
| **400** Bad Request | the request is wrong: two framings (65.21), or a body length that cannot be determined (65.22). The sender has to fix it. |
| **501** Not Implemented | the request is fine and the server is not capable of applying a coding in it. The sender has to pick a different server. |

So the parser's reason is **not** a protocol error. The request is well
formed and the framing is determinate. The reason is "not implemented",
and it travels out of the parser in `errno` (`ENOTSUP`, against `EPROTO`
for everything else) so the server can choose the status from it — and
the server reads it *before* anything else can clobber it.

## 3. A MUST outranks a SHOULD, and the order is part of the rule

The three rules about the coding header now form one decision tree, and
they are checked in the order the RFCs put them in:

| the request | the answer | the rule |
|---|---|---|
| a coding **and** a length | **400** | §6.3 item 3 — two framings (65.21) |
| final coding **not** chunked | **400** | §6.3 item 4 — a **MUST** (65.22) |
| final coding chunked, an earlier coding unknown | **501** | §6.1 — a **SHOULD** (this stage) |
| final coding chunked, all codings known | **200** | ✓ |

`Transfer-Encoding: gzip` is a **400**, not a 501: the body length
cannot be determined reliably (§6.3 item 4, a MUST), and a MUST
outranks a SHOULD. `Transfer-Encoding: gzip, chunked` is a **501**:
the framing is determinate, and the coding in front of the chunked one
is one we cannot apply.

**The order is load-bearing, and that is the second proof (§5).**

## 4. One reading of the list answers both questions

The stage needed two facts about the coding list — *is the final coding
chunked?* and *does the list name a coding we do not understand?* — and
the old code had one helper that answered the first by returning a
name. So the helper became a **scan** that answers both in one pass:

```c
typedef struct {
    int final_is_chunked;   /* the last non-empty element is "chunked" */
    int has_unknown;        /* some non-empty element is a coding we cannot apply */
} coding_scan;
```

Every non-empty element is a coding; the last one seen is the final
coding; and *any* element we cannot apply sets `has_unknown`. The empty
elements are skipped, which is the RFC 9110 §5.6.1 rule Stage 65.22
established: an empty element is not a coding, so it is neither the
final coding nor an unknown one.

`request_final_coding()` (which copied the name out) is gone; the scan
answers the question it was asked.

## 5. The measurements

| | |
|---|---|
| new tests | **9/9** (`build/stage_65_23`) |
| component total | **251** across **25** binaries |
| full suite | `make test` → exit **0** |
| valgrind | **0 errors / 0 leaks** on all twenty-five binaries — **146** clean `ERROR SUMMARY` lines (one per binary *and* per forked child), `VG_EXIT=0` |
| discrimination proof A | with the **rule** removed: **3 of 9 pass, 6 FAIL**; restored: **9/9** |
| discrimination proof B | with the **order swapped** (the SHOULD checked before the MUST): **8 of 9 pass, 1 FAIL** |
| live check | **curl**, a raw socket client, and **Node** (§6) |

**The red run was honest and complete: 3/9.** The three that passed were
the ones that should — "the MUST outranks the SHOULD", "a coding we
understand is still answered", and "a plain request is still answered".
The six that failed were the three server-level refusals, the
parser-level refusal, the case-insensitive name, and the empty-element
case.

**Two proofs, because the stage changed two things.** Proof A removes
the rule: six tests fail. Proof B keeps the rule and swaps the *order*:
only "the MUST outranks the SHOULD" fails, and it fails because
`Transfer-Encoding: gzip` is answered 501 instead of 400 — which is
exactly what a SHOULD-checked-first implementation would do, and exactly
what the RFC does not want.

**The count moved.** Stage 65.22's sixth test asserted that a coding
list ending in chunked is framed by chunks — that is, that
`gzip, chunked` is *answered*. This stage deliberately replaced that
behavior, so rather than leave a test asserting a behavior the
component no longer has, the test was removed and the claim moved here
(inverted). 243 − 1 + 9 = **251**.

## 6. The oracle: curl, a raw socket, and Node

**curl** can produce this request on purpose — a header it adds, a body
it does not send, so there is no `Content-Length` to confuse the
picture:

```
$ curl -v -H "Transfer-Encoding: gzip, chunked" http://127.0.0.1:PORT/echo
> GET /echo HTTP/1.1
> Host: 127.0.0.1:18081
> User-Agent: curl/8.5.0
> Accept: */*
> Transfer-Encoding: gzip, chunked
>
< HTTP/1.1 501 Not Implemented
< Date: Thu, 12 Nov 2026 19:52:22 GMT
< Content-Type: text/plain
< Content-Length: 20
< Connection: close
```

The raw socket client covers the ordering case, which is the request
that separates a rule about the *final* coding from a rule about the
*presence* of a coding:

```
Transfer-Encoding: chunked, gzip  ->  HTTP/1.1 400 Bad Request   (the MUST still wins)
Transfer-Encoding: gzip, chunked  ->  HTTP/1.1 501 Not Implemented
```

And the paths that must keep working, live on the same server: a plain
GET → **200**, a chunked upload → **200** with the body
(`Hello, chunked world!\n`).

**Node is the interesting one.** Node's own HTTP server, given the very
same request, answers **200** — and hands back the still-gzip-coded
bytes, which is the failure this stage closes, on a server that is not
ours. That is worth writing down plainly: **this rule is a SHOULD, and
the implementations differ.** nginx answers 501 for an unknown transfer
coding; Node answers 200 and hands back bytes it did not decode. There
is no oracle in the sense of "another implementation that agrees",
because the RFC asked for agreement and did not get it. The authority
here is the RFC, and the point of the stage is that a server which
*chooses* the SHOULD is a server that says what it cannot do instead of
pretending it can.

## 7. The findings

- **A framing you can read is not a coding you can decode.** Knowing
  where the body ends is not knowing what it says. The two are separate
  capabilities, and a server that has the first and not the second has
  to say so.
- **400 and 501 are claims about who is at fault.** "This request is
  wrong" and "this server is not capable" are different sentences, and
  the status code is where a sentence like that lives.
- **A MUST outranks a SHOULD, and the order of the rules is part of
  the rule.** The same request is a 400 or a 501 depending on which
  rule is checked first, and the RFCs settle it.
- **The reason has to travel from the parser to the server.** The
  parser knows *why* it refused; the server knows what to say. One
  `errno` value carries the difference (`ENOTSUP` against `EPROTO`),
  and the server reads it before anything can clobber it.
- **A SHOULD is not a MUST, and the implementations differ.** Node
  answers 200 to the request this stage answers 501. The RFC asked for
  agreement and did not get it, so the authority here is the RFC, not
  another server.

## 8. What this stage deliberately does *not* do

- **The client's mirror** — a *response* carrying a coding we do not
  implement. The client reads the chunks and hands back the
  still-gzip-coded bytes: the same silent failure, on the other side
  of the wire. **This is the next stage.**
- **Duplicate or unequal `Content-Length` headers** → 400 (RFC 9112
  §6.3 item 5, a MUST). The parser takes the first one, and a list of
  equal values ("Content-Length: 5, 5", which the RFC says to accept
  as 5) is refused as a non-number.
- **HTTP/1.0 with a `Transfer-Encoding`** → the framing is faulty and
  the connection closes (RFC 9112 §6.1, a MUST). Our server already
  closes after an HTTP/1.0 response, but it does not treat the
  framing as faulty.
- **The gzip coding itself** — decoding it would remove this stage's
  501 for `gzip, chunked` (and not for `gzip` alone, which stays a
  400).
- No `HEAD`, no `405`, no TLS, no `SIGPIPE`.

## 9. Next

**Stage 65.24 — the client refuses a response whose transfer coding it
does not understand**: the mirror of this stage, on the other side of
the wire. Today the client takes the chunks apart and hands back the
still-gzip-coded bytes as the body — the same silent failure, in the
role that has to *use* the body rather than the role that has to
*frame* it. The client's own final-coding scan is also the old scan
(Stage 65.22 fixed the server's), so it reads `Transfer-Encoding:
chunked,` as "no coding" — the same empty-element bug, on the other
side.

### What would change the pick

- A steer to `HEAD` (RFC 9110 §9.3.2) or `405` — the smaller HTTP
  conformance gaps.
- A steer to the browser's **outer-loop iteration cap** (it needs a
  *decision about deliberately diverging from html5lib* before it needs
  code).
- A steer to the `main` rename — naming only; nothing depends on it.
- A steer to the deferred sockets items: TLS, `SIGPIPE`, CNAME chasing,
  EDNS0, TCP fallback, larger benchmarks, a CPU-bound handler, warm-up —
  and the one-shot `http_client_get` write loop, which still does not
  treat a zero-byte write as an error.
- A steer to the Vercel publish gate (the site has been stale for
  eighteen cycles) instead of another stage.
- Silence does not change the pick.
