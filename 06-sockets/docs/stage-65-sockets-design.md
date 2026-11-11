# Stage 65 — Sockets from scratch (design draft)

**Author:** Mira
**Date:** 2026-11-11
**Status:** DESIGN DRAFT — no code yet. Stage 65.1 does not start until this doc exists and Tom has had a chance to redirect the target.
**Trigger:** Stage 64 close-out — "Sideways systems bite (retrospective B)": apply the stage ladder to a *different* project; retrospective #3 Option B says "unblocked at any time."
**Veto:** Tom can redirect the target in one message (see §8). The pick is mine; the aim is his.

---

## 1. Why this component now

The curriculum has taught: memory (01), persistence (02), processes (03), the DOM and the
wire protocol it speaks (04), and a language from the ground up (05). Every stage so far
has been about **one machine talking to itself**.

It has never been about **two machines**.

That is not an academic gap. It is the gap my own house lives in. The gateway relays
model calls to `mi50` over an OpenAI-compatible endpoint; the Telegram, Discord and
Mattermost bridges are all socket clients; `gws`, `xcli` and the MCP servers are all
socket clients; the A2A gateway on this box is a socket *server*. Today I spent an
afternoon on a bug whose root cause was protocol shape: the relay probed `/api/tags`
(Ollama) on a host that speaks `/v1/chat/completions` (llama.cpp), and — because a
probe for *presence* and a probe for *protocol* look identical from the outside — it
concluded the machine was absent. **"Is anyone home?" and "do we speak the same
language?" are different questions, and only the second one is asked by a socket.**

A curriculum that has never opened a socket cannot ask that question well.

## 2. Goals / non-goals

### Goals (the arc, in C, on Linux)

1. **A socket is a file descriptor.** Build up from `socket()`/`bind()`/`listen()`/`accept()`
   with no wrapper library, so the ownership of a connection is legible.
2. **Addresses are bytes.** Hand-pack `sockaddr_in`, do the byte-order conversions by hand
   once, then use the library calls and know what they do.
3. **Streams have no message boundaries.** Learn short reads, partial writes, and framing
   as a consequence of TCP, not as a quirk to work around.
4. **One HTTP server and one HTTP client, written from the wire up.** No framework.
5. **One DNS exchange, hand-built.** Query packet in, response packet out.
6. **Two concurrency models, on the same server.** Process-per-connection and
   event-loop-per-connection — and the *tradeoff* between them, felt rather than recited.
7. **Tests first, valgrind always.** This component is C, so the valgrind step in the
   loop is no longer N/A. It becomes the component's ground truth.

### Non-goals (explicitly later)

- TLS/SSL, HTTP/2, QUIC, HTTP/3 — the cryptography and the protocol versions are not the
  lesson, and every one of them is a way to avoid the lesson.
- Writing a browser, a language, or a database *again* — this arc reuses 05's language
  and 04's DOM only if a stage genuinely needs them.
- A production-grade server: no worker pools, no zero-copy, no io_uring, no eBPF.
- A DNS *resolver* (hosts file, nsswitch, `/etc/resolv.conf`) — the wire format is the
  lesson; the resolver configuration is not.
- Any protocol built "because it would look good in a demo" — the arc earns one protocol
  (HTTP) and one lookup (DNS), and both must be used by the tests.

## 3. The shape of the ladder

Nine stages, each one independently shippable, each with its own close-out. The ladder
runs **bottom-up**: the lowest level of the stack first, and HTTP only when the byte
layer is already understood.

| Stage | Subject | The lesson it proves |
|---|---|---|
| **65.1** | fd lifecycle: `socket` → `bind` → `listen` → `accept` → `read`/`write` → `close` | A connection is a kernel object; the process owns only an integer |
| **65.2** | addresses by hand: `sockaddr_in`, `htons`/`htonl`, `inet_pton`/`inet_ntop`, `getsockname` | An address is a byte string in network order, not a name |
| **65.3** | the accept loop + `fork()` per connection | Two queues exist (backlog ≠ accept queue); one process per connection is the simple answer |
| **65.4** | errors as protocol: `errno`, `EINTR`/`EAGAIN`, short reads, short writes, `SO_REUSEADDR` | `read()` returning less than asked is normal, not an error |
| **65.5** | non-blocking + `select()`/`poll()` multiplexing with per-connection buffers | A stream has no message boundaries — you must frame it yourself |
| **65.6** | HTTP/1.1 server from the wire: request line, headers, `Content-Length`, keep-alive, 404 | HTTP is text with rules; the server is a state machine over bytes |
| **65.7** | HTTP/1.1 client from the wire (no `curl`, no libraries) | The client side is where framing bugs bite first |
| **65.8** | DNS from scratch: build the query packet, parse the response, cache it | A lookup is a UDP exchange, not a function call |
| **65.9** | two concurrency models on one server: `fork()` vs event loop, measured | The tradeoff is throughput and latency, not taste |

**Ordering note:** 65.5 before 65.6 is deliberate. An HTTP server written on top of a
byte-stream model you do not yet understand produces a server whose bugs are all framing
bugs. Framing first, protocol second.

## 4. How we test

The loop's rules, made concrete for C:

- **Tests first.** Each stage's test file exists and fails before the implementation
  exists. The close-out records the red symptom.
- **Hermetic.** Loopback only. No test asserts on another host's response — the lesson
  from `04-browser`'s rotted example.com fixture, which taught the expensive way that
  a test asserting about someone else's page is not testing our code.
- **Valgrind on every stage.** No stage closes with an unaccounted leak or an
  invalid read. The stage's close-out carries the valgrind summary.
- **A reference implementation as oracle.** Where a correct answer exists (DNS packet
  layout, HTTP status lines), cross-check against a known-good implementation the way
  slice 2 of 04-browser cross-checked its expected trees against html5lib — and remember
  that **the verifier must be verified** (twice, that day, my fault both times).
- **One lesson written down per stage**, in the stage's close-out, in the component's
  own words. That is the education, not the code.

## 5. Alternatives considered

| Option | Why not |
|---|---|
| **A concurrency ladder** (threads → mutexes → work-stealing) | Real and tempting — the house runs a concurrency-shaped thing. But it is *orthogonal* to I/O: it teaches synchronization, not the wire. A good third option; a worse first one, because the protocols are the gap. |
| **A different storage model** (LSM / log-structured) | Also real. It overlaps 02-database too much to be a new bite — it would be a re-run of an arc already walked. |
| **Rebuild the browser's networking in Rust** | Doubles down on a component that is already verified green (346 tests, 0 failed). Low marginal value, high coupling. |
| **A sockets ladder in Rust** | Tempting — Rust's std has `TcpStream`, so the stages would be shorter. But the *lesson* is the kernel interface and the byte order and the framing; Rust hides exactly those. The arc should be C. |

## 6. What the house gains

- The relay's protocol probe becomes something I can reason about from the inside:
  I will know that `/v1/chat/completions` and `/api/tags` differ in *shape* (a
  JSON-RPC-ish POST vs a text-model list), not just in spelling.
- When a bridge fails to reconnect, I will know whether it is a socket problem, a
  protocol problem, or a peer problem — and the failure modes of those three look
  identical from the outside.
- The `mi50` llama.cpp server, the A2A gateway, the MCP servers, the SSH bastion:
  all socket servers I depend on and cannot see inside. After this arc they are
  not black boxes; they are known shapes.

## 7. What would change this pick

- **Tom redirects the target.** Concurrency ladder, storage model, or something in
  between — one message is enough, and the arc should follow the aim, not my ranking.
- **The house needs something else first.** If the xAI token comes back, or Vercel
  unblocks and the publish backlog needs essays, the arc waits — those are Tom's
  visible problems and they outrank my curriculum.
- **A spec example in 04-browser fails.** The active-formatting-elements list and the
  `<a>` special case are still unimplemented; if the parser earns another slice
  (a spec example that we get wrong), that outranks a new component.
- **The arc's first stage proves too easy.** If 65.1–65.3 turn out to be an hour of
  `man` pages rather than a stage, the ladder is wrong and gets reshaped before
  stage two.

## 8. Lesson to be written down (the arc's own)

*To be filled at 65.9, in the arc's own words:* what a network stack costs, and what
"the protocol is the program" means when the program is 400 lines of C.
