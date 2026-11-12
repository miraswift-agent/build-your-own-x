# Stage 65.8 — DNS from scratch, first half: the packets (close-out)

**Date:** 2026-11-11 (CST evening) · **Branch:** `stage-65-sockets` · **Commit:** `20a0344`
**Component:** `06-sockets` (C) · **Predecessors:** 65.1 `56ce410`, 65.2 `301aad2`, 65.3 `c7a727e`, 65.4 `c2ec574`, 65.5 `9828192`, 65.6 `bc84304`, 65.7 `095d0f0`

---

## 1. The lesson

**A DNS message is not a string. It is a length-framed structure whose
names may be compressed — and the compression is where every hand-rolled
DNS implementation breaks.**

Stage 65.6 learned that HTTP frames itself *in the text*. DNS frames
itself in binary, and it does it in three ways that each cost a
distinct kind of care:

- **Every multi-byte field is network byte order** — big-endian. The
  header's id, the flags, the four counts, the type, the class, the
  TTL, the rdlength. All of them.
- **A name is a sequence of labels**, each a length byte followed by
  that many bytes, terminated by a zero byte. The root is the empty
  name — one zero byte. A label is at most 63 bytes; a name is at most
  255.
- **A name may be compressed.** If the top two bits of the length byte
  are `11`, the rest of that byte and the next byte are a 14-bit
  offset *back to an earlier name*, and **the name ends at the
  pointer**. Compression exists because a response repeats the
  question's name in every answer record, and a typical answer is
  mostly the question's name again.

The third rule is the one that bites. A pointer does not have to point
at the *start* of a name. It can point at the middle of one. Two of my
test expectations were wrong about exactly that (see §5.2), and the
wire was right both times.

And the rule that has shown up in every stage of this component: **the
length of a record's data is given** (RDLENGTH). You never scan for a
terminator inside it. That is `Content-Length` again, in binary, and it
is the difference between a parser and a guesser.

## 2. What changed

New files: `src/dns.h`, `src/dns.c`. Nothing else in the component
touched — DNS packets do not need a socket yet, which is the point of
splitting the stage.

**Names.** `dns_name_encode()` turns `"example.com"` into
`07 'e' 'x' 'a' 'm' 'p' 'l' 'e' 03 'c' 'o' 'm' 00`. `dns_name_decode()`
walks labels, follows `0xC0` pointers, and rejects a pointer that
loops, points at itself, or points outside the message.

**Queries.** `dns_build_query()` writes a standard recursive query: a
12-byte header (id, flags `RD`, one question, no answers) plus the
name, the type and the class.

**Messages.** `dns_parse_header()`, `dns_parse_question()` and
`dns_parse_record()` give back the counts, the rcode, the question,
and the records — with `rdata` pointing into the caller's buffer rather
than being copied, because the caller already has the bytes.

## 3. The oracle: captured wire bytes, not my imagination

Stage 65.6's tests checked my parser against my own encoder, which
proves the two agree and not that either is right. The design doc asks
for better:

> *A reference implementation as oracle. Where a correct answer exists
> (DNS packet layout, HTTP status lines), cross-check against a
> known-good implementation… and remember that **the verifier must be
> verified**.*

So the fixtures in `tests/test_stage_65_8.c` are **actual bytes**.
They were captured by putting a UDP relay on `127.0.0.1:5354`,
pointing `dig` at it, and forwarding to the system resolver
(`127.0.0.53`):

```
dig @127.0.0.1 -p 5354 example.com A +noedns
```

The relay logged both directions. What came back:

- **dig's query** for `example.com` A — 29 bytes, and the name encoding
  in it is exactly what `dns_name_encode()` produces.
- **an A response** — 61 bytes, two answers, both with the answer name
  as a `c00c` pointer back to the question's name.
- **an NXDOMAIN response** — 116 bytes, no answers, an SOA in the
  authority section whose rdata contains a pointer *inside* the SOA
  data.
- **a NODATA response** — 95 bytes, no answers, an SOA in the authority
  section whose owner name resolves through a pointer.
- **a null MX** — 44 bytes, one answer, preference 0.

The oracle was verified before being trusted. `dig`'s own answer text
is the ground truth for what the bytes mean: it printed
`172.66.147.243` and `104.20.23.154`, and the test asserts the parser
extracts exactly those addresses from the same bytes. The two dig
defaults that had to be handled explicitly are named in the test file:

- **dig sets the AD bit** in its queries (`0x0120` = RD | AD) because
  it asks for DNSSEC data by default. We set RD only (`0x0100`), so the
  comparison masks the AD bit — and the mask is on byte 3, the low byte
  of the flags word. (It was on byte 2 at first, which is the high byte,
  and the test failed for a reason that had nothing to do with DNS.)
- **the ids are random**, so the tests assert on the id that is in the
  fixture rather than pretending ids do not exist.

## 4. The tests

23 tests:

- **encoding** — a name exactly as dig sends it; the root as one zero
  byte; a 64-byte label rejected; a 300-byte name rejected; no writing
  past the buffer.
- **queries** — our A query is byte-identical to dig's; our CNAME
  query is byte-identical to dig's.
- **decoding** — a plain name; the root; a compression pointer; **a
  pointer inside rdata**; a pointer that loops; a pointer past the end
  of the buffer; **the offset after a compressed name**.
- **messages** — the header; the question; an A record (address, TTL,
  rdlength, and the name resolved through its pointer); walking every
  answer; NXDOMAIN; the SOA in an authority section; a null MX;
  NODATA; **truncation reported rather than guessed**.

The component total is now **115/115**: 8 (65.1) + 8 (65.2) + 8 (65.3) +
13 (65.4) + 18 (65.5) + 21 (65.6) + 16 (65.7) + 23 (65.8).

## 5. The findings

### 5.1 An API that returns an absolute offset invites exactly one mistake

`dns_name_decode()` hands back **where the next field starts**. Both of
its callers treated that as a **length**, and added it to `off` again.

The symptom was absurd, which is what made it findable: a question
whose **type read as 300 and whose class read as 4**. Those are not
type and class values at all — 300 is the TTL of the answer and 4 is
its rdlength. The parser was reading the answer's fields and calling
them the question's.

The test that asserts on the offset — *the offset after the original
encoding is 25* — **passed the whole time**, because the offset was
right. Only the callers were wrong. A test cannot catch a bug that
lives in how a correct value is used, and this is the sharpest
reminder in the component that **the API is the thing under test, not
just the function**.

The fix is a comment at the call site and a rename (`after`, not
`used`), because the name `used` is what invited the mistake.

### 5.2 Two wrong expectations about where a pointer points

Both of these are test bugs, and both are the same shape: I wrote the
expectation from intuition about what a pointer "should" point at, and
the wire disagreed.

- The NXDOMAIN response's SOA has an RNAME ending in a pointer. I
  expected it to resolve to the queried name. It resolves to **`com`** —
  the pointer lands on the *middle* of the query's name, on the `com`
  label. A pointer points at a name; a name can start anywhere.
- The NODATA response's SOA owner name is reached through a pointer to
  offset `0x10`. I expected `www.example.com`. It is **`example.com`**
  — the zone the SOA belongs to, not the name that was asked about.

Both were settled by counting offsets in the hex, not by reasoning. The
generalisation: **a compression pointer is an offset into the message,
and the only way to know what it means is to decode what is there.**
Any expectation about a compressed name is a guess until the bytes say
otherwise.

### 5.3 A guard test from Stage 65.2 caught me four stages later

Stage 65.2's rule is "addresses by hand — the endpoint is a byte string
we assemble, no library address helper anywhere in `src/`", and it is
enforced by a test that scans the `src/` directory for `htons`,
`ntohs`, `inet_pton`, `inet_ntop`, `inet_addr` and `getaddrinfo`.

My `dns.c` tripped it. Not by *calling* a helper — by **mentioning** one
in a comment (`#include <arpa/inet.h> /* htons / ntohs */`). The stage
65.2 suite went from 8/8 to 7/8 and the failure message was the file
name: `src/dns.c still uses htons`.

So the fix was to honour the rule properly: the include came out, the
manual shifts stay, and the comment now says what the shifts are for
without naming a helper the component does not use. The rule outlived
the stage that wrote it by four stages, and it caught a violation that
a human reviewer would probably have let slide.

That is the whole argument for writing a rule down as an **executable
check** instead of as prose. Prose in a close-out is advice; a test is
a gate. Stage 65.2's gate was still doing its job when I walked in.

## 6. Valgrind

**0 errors, 0 leaks** across all eight binaries and every forked child
(36 valgrind processes, exit status 0 — `--error-exitcode=1` means any
leak or invalid access would have failed the build). "All heap blocks
were freed -- no leaks are possible."

## 7. What was left on the table, deliberately

- **The transport and the cache** — the other half of the design doc's
  65.8. Sending the query over UDP, reading the response, caching it.
  That is the half that needs a socket, and it is the next stage.
- **EDNS0.** dig's default query carries an OPT record in the
  additional section; the fixtures here were captured with `+noedns`
  so the messages stay minimal and the layout stays honest.
- **TCP and the TC bit.** A response that does not fit in 512 bytes is
  truncated, and the real answer arrives over TCP. `dns_is_truncated()`
  exists and is unused; that is a deliberate loose end.
- **AAAA, CNAME chasing, DNSSEC validation.** The parser can read a
  CNAME record; it does not follow one. Chasing is a loop in the
  client, not a feature of the packets.
- **The 512-byte limit as an error.** A DNS message over UDP that
  exceeds it is not "too long", it is truncated — a different answer.

## 8. Provenance: the split worked

This stage is the second half of a stage that was deliberately split in
two, because two cycles in a row had blown the 900-second judgment cap.
The first half (the packets) was scoped to fit a window, and it did:
the judgment cycle that produced it ran **about eight minutes of work**
inside a fifteen-minute window, with time to spare for the close-out,
the wiki, and the push.

The split is the right lesson of Stage 65.4-65.8 together: **a stage
has to be sized to the window, not to the ladder.** The ladder said
"DNS"; the window said "DNS in two halves, and the half that is a
packet library first".

## 9. Next

**Stage 65.9 — DNS from scratch, second half: the transport and the
cache.** Take the packets from this stage, send them over UDP, read
the response, cache the answer, and go back to the resolver when the
cache entry expires. The lesson there is not DNS at all: it is that a
**lookup is an exchange, not a function call**, and everything the
exchange can do — time out, come back truncated, come back NXDOMAIN —
is a result the caller has to handle.

The design doc's concurrency measurement (65.9, now renumbered 65.10)
follows it: `fork()` versus an event loop, measured on one server.

### What would change the pick

Tom can redirect the target in one message — the pick is mine, the aim
is his. Any of these moves 65.9 aside: a steer to close the sockets arc
after 65.8 and go back to the clox ladder; a steer toward the browser's
one genuinely open item (the active-formatting-elements list and the
`<a>` special case); a request to skip the DNS transport and measure
the concurrency models now; or a request to spend the time on the
Vercel publish gate instead of the ladder.

What would **not** change the pick is silence. The ladder is mine to
walk, and the other half of the stage I started is the next rung.
