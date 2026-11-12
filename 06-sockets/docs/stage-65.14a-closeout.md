# Stage 65.14a — the parser knows where the next message starts — close-out

**Date:** 2026-11-12 · **Component:** `06-sockets` (C) · **Code:** this commit
**Branch:** `stage-65-sockets` · **Tests:** 5 new (component total **177**)

---

## 1. What shipped

The parser no longer throws away the bytes that follow a response.

A connection carries messages back to back. A response can end before
the bytes that follow it have been read: the peer has already sent the
next response, and it arrived in the same read as the end of this one.
The parser must **keep** those bytes, and the next call must find them
there.

```c
size_t http_response_parser_pending(const http_response_parser *p);
```

The parser says how much it is holding, so the claim is observable from
outside and not just from the inside.

## 2. The finding: the chunked framing hides the leftovers

The length-framed path already did this correctly — after the body it
memmoves whatever followed to the front of the buffer and keeps it. The
chunked path did not, and the reason is structural:

> The chunk framing is consumed by the **chunk decoder**, so the bytes
> after the terminal chunk end up inside the decoder rather than in the
> parser's buffer — and a parser that only looks at its own buffer
> loses them.

So the decoder had to be able to say what it did *not* consume:

```c
size_t http_chunk_decoder_pending(const http_chunk_decoder *d, const char **out);
```

and the parser takes those bytes back for the next message.

The gap was specific to chunked, and it went unnoticed **because the
client only ever made one request per connection** — there was never a
second message to lose.

## 3. The oracle

Node's captured response again — the same 199 bytes Stage 65.13 used.
The question here is not *"what does a chunked body mean"* (Stage 65.13
answered that) but *"what happens to the bytes that come after it"*.

So the fixture is that response, and the tests put **two** of them in
one buffer, then mix framings:

| test | what it pins down |
|---|---|
| two chunked responses in one buffer | the second response is found, byte for byte |
| chunked, then length-framed | the leftover survives a framing change |
| length-framed, then chunked | a **fresh decoder** per response |
| the next message arrives in pieces | the leftover can still be incomplete |
| the parser says how much it holds | the leftover is observable |

## 4. The measurements

| | result |
|---|---|
| new tests | **5/5 passed** |
| component total | **177** (was 172) |
| valgrind | **0 errors, 0 leaks**, 14 binaries, exit 0 |

**The tests were checked against the old behavior**: with the leftover
bytes dropped, **3 of the 5 fail**. A test that passes either way
proves nothing.

> The *first* attempt at that proof did not run. The temporary edit
> failed to compile (`-Werror` caught the now-unused variable), the
> build stopped, and the test ran the **previous** binary — which
> passed, 5/5, and would have been a comfortable lie. The proof was
> redone with the build's exit code checked, and then it failed, as it
> should have.

## 5. The lessons

**A framing decides where a message ends; the connection decides where
the conversation ends.** The bytes in between belong to the next
message.

**A bug can be specific to a framing and still be a bug in the
connection layer.** The chunked path dropped bytes the length path
kept, and the difference was invisible until something was expected to
follow.

**A proof that did not execute proves nothing.** The first version of
the check above reported 5/5 — from a binary that predated the change.

## 6. What this stage deliberately does *not* do

- **No connection object.** The client still sends `Connection: close`
  and still makes exactly one request per socket. This stage gave the
  parser the ability to carry a second message; nothing yet *asks* for
  one.
- **No reuse.** A response framed by the close of the connection ends
  the conversation — there is no next message on that connection, and
  the parser empties itself when it completes one.

## 7. Next

**Stage 65.14b — the connection object, and a second request on the
same socket.** The client needs to hold a socket, write a request, read
a response, and then write *another* request on the same socket —
without the server closing in between.

The oracle is on the wire, not in our opinion of it: **a second
request on the same socket must get a second response, and the server
must not have closed in between.** That means the client must also
stop sending `Connection: close`, which is a change to the request
builder — and the existing one-request API has to keep working.

### What would change the pick

Tom can redirect this in one message: a steer to the `main` rename (the
OPEN question in `open-questions-for-tom.md` — a naming change, not a
capability); a steer to the browser's **outer-loop iteration cap** (the
documented single-behaviour difference from the spec — it needs a
decision about deliberately diverging from html5lib before it needs
code); a steer back to the clox ladder; a steer to the sockets
component's other deferred items (`Host` routing, `Date:`, TLS,
`SIGPIPE`, CNAME chasing, EDNS0, TCP fallback); or a request to spend
the time on the Vercel publish gate instead. Silence does not change
the pick.

One thing that *would* change it, without Tom: if the connection object
turned out to need a design decision this component has not made yet —
say, how the client decides a connection is dead — that decision is
worth its own stage rather than a guess inside 65.14b.
