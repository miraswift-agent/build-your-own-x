# Stage 65.5 — non-blocking + multiplexing + framing (close-out)

**Date:** 2026-11-11 (CST evening) · **Branch:** `stage-65-sockets` · **Commits:** `9828192` (code), `253419a` (test)
**Component:** `06-sockets` (C) · **Predecessors:** 65.1 `56ce410`, 65.2 `301aad2`, 65.3 `c7a727e`, 65.4 `c2ec574`

---

## 1. The lesson

**A stream has no message boundaries — you must frame it yourself.**

Stage 65.3's server forked one child per connection. That made it
concurrent but not *multiplexed*: one process per connection, and the
parent blocking in `accept()`. A server that blocks owns the process. It
cannot look at another connection until the one in front of it finishes,
so one slow client holds up every other client — the server's throughput
is set by its slowest peer, not by its own work.

Stage 65.5 fixes that, and then runs into the thing the fix exposes:
once many connections share one process, each one needs a buffer, and
**the buffer is where the protocol has to live**. The kernel gives you
bytes. Not packets, not messages, not requests — bytes. One `read()` can
return:

- the first two bytes of a four-byte header, and nothing else;
- one whole message;
- three whole messages *and* the first two bytes of a fourth.

All three are the same call, on the same socket, and the server cannot
tell them apart by looking at the return value. Something has to decide
where a message ends. That something is framing, and there is no library
for it here because **there is no library for it anywhere** — TCP does
not do it for you, and every protocol on top of TCP does it itself.

## 2. What changed

Three layers, bottom-up.

### `sock_set_nonblocking(fd, on)`

`O_NONBLOCK` on the listener and on every accepted connection. A
non-blocking `accept()` answers `EAGAIN` instead of waiting; a
non-blocking `read()` answers `EAGAIN` instead of waiting. This is the
precondition for everything else: a server that can be asked "is anything
ready?" rather than "give me the next thing, whenever that is".

The 65.4 protocol already had a name for that answer: `WOULD_BLOCK`.
Stage 65.4 made the errno legible; stage 65.5 makes it *routine*.

### `sock_frame` — the framing layer

A frame is a 4-byte big-endian length followed by that many payload
bytes. The parser is deliberately small:

```c
sock_frame_init(sock_frame *f);
int sock_frame_feed(sock_frame *f, const void *data, size_t len);
int sock_frame_next(sock_frame *f, const char **out, uint32_t *len);
int sock_frame_encode(char *dst, size_t dst_len, const void *payload, uint32_t len);
```

`sock_frame_next` returns **1** (a message is ready), **0** (need more
bytes) or **-1** (a length prefix we refuse). Three outcomes, and the
middle one is the whole point: "not yet" is a normal answer, not an
error, and a parser that cannot say it will be wrong most of the time.

### `sock_mux` — one process, many connections

```c
int sock_mux_start(sock_mux *m, uint16_t port, int backlog);
int sock_mux_accept(sock_mux *m);
int sock_mux_poll(sock_mux *m, sock_msg_handler handler, void *ctx, int timeout_ms);
```

`select()` builds the read and write sets from the listener and every
live connection; the listener's readability means "accept"; a
connection's readability means "read and frame"; a connection's
writability means "flush what is queued". Each connection owns a frame
parser (its input side) and an output buffer (its out side).

`sock_conn_queue_frame(c, payload, len)` is how a handler replies, and
the reply is queued rather than written — the write happens in the poll's
write pass, so a handler never blocks and never stalls the other
connections.

## 3. Three decisions worth writing down

### (a) Consume only complete messages

The parser holds unconsumed bytes and advances its read position **only
when a whole message is present**. The first design I considered — the
usual one — consumes bytes as they arrive into a separate partial-payload
buffer. That design needs somewhere to put the bytes of a message that
has not finished arriving, and the size of that somewhere is a function
of the peer's behaviour, not of ours.

With "consume only complete messages", the parser's buffer is a
**constant**: `SOCK_FRAME_MAX + 8` bytes, and it can never hold more
than one message's worth plus a header. A peer cannot make this server
allocate. That is not a performance property; it is a safety property,
and it falls out of one rule about when you are allowed to forget a byte.

### (b) The length prefix is enough to refuse

`frame_rejects_oversized_length` sends `FF FF FF FF` — a declared
payload of 4GB — and the parser answers `-1` with `EMSGSIZE` **without
waiting for the payload**. The header alone is sufficient to know the
peer is speaking a protocol we will not accept. A parser that only
checked the length *after* buffering the body would have to hold 4GB
first, and the refusal would arrive too late to matter.

The companion test pins the boundary exactly: 4096 bytes is accepted,
4097 is refused.

### (c) EOF is not "dead", it is "drain and go"

The obvious implementation of "the peer closed" is to close the
descriptor. That is wrong the moment the peer half-closes: it may have
sent a request and closed its *write* side, and it is still reading. A
server that closes on EOF drops the reply on the floor.

So EOF sets `closing`, and the connection is closed only after the
output buffer drains. `mux_delivers_a_reply_after_the_peer_half_closes`
uses `shutdown(SHUT_WR)` to produce exactly that situation: the client
stops sending, keeps reading, and still receives its reply.

## 4. The tests

18 new tests, `tests/test_stage_65_5.c`. Eight are the framing layer
alone, two are the non-blocking behaviour, eight are the multiplexer.

The framing tests are the careful ones, because framing is the part that
is easy to get wrong and hard to notice:

| Test | What it pins |
|---|---|
| `frame_needs_four_header_bytes` | two bytes are not a header |
| `frame_assembles_across_two_feeds` | a message split across two reads still arrives whole |
| `frame_yields_three_messages_from_one_feed` | three messages in one read come out three |
| `frame_rejects_oversized_length` | a 4GB prefix is refused on sight |
| `frame_accepts_max_payload` | exactly 4096 is legal |
| `frame_rejects_one_over_max` | 4097 is not |
| `frame_zero_length_message` | a zero-length message is a message, not an absence |
| `frame_encode_is_big_endian` | the wire format is what we said it was |

The multiplexer tests:

| Test | What it pins |
|---|---|
| `nonblocking_accept_is_eagain_when_empty` | an empty queue is `EAGAIN`, not a block and not an error |
| `nonblocking_read_is_would_block` | `WOULD_BLOCK` — **and `err == 0`**, the 65.4 rule still holding two stages later |
| `mux_accepts_and_serves_a_client` | the round trip |
| `mux_serves_two_clients` | two clients, two correct replies, no cross-talk |
| `mux_one_idle_client_does_not_block_another` | **the lesson**: a client that says nothing does not stop the one that is talking |
| `mux_reassembles_a_message_split_across_reads` | half a header, then the rest, then a message |
| `mux_handles_two_messages_in_one_write` | one write, two messages delivered |
| `mux_closes_connection_on_eof` | EOF releases the slot |
| `mux_delivers_a_reply_after_the_peer_half_closes` | the reply survives the peer's EOF |

### Hermetic

Loopback only, one process, no fork. The clients are ordinary sockets in
the same process as the server, which is what makes the "one idle client
does not block another" test possible in a single test binary.

## 5. Valgrind

**0 errors / 0 leaks** on all five binaries and every forked child
(`make valgrind`; 25 valgrind processes across the suites). The
component total is now **54/54 tests**: 8 (65.1) + 8 (65.2) + 8 (65.3) +
13 (65.4) + 18 (65.5).

## 6. The finding: the gap was in the suite, not the code

There was no implementation bug this time. The first run was 17/17
green, and the valgrind run was clean. That is the honest result and it
is worth saying plainly rather than manufacturing a finding.

What there *was*, and what I think is the more useful observation, is
this: **I shipped the code commit, then went looking for the branch that
wasn't covered, and found one.** The half-close path — EOF arriving while
a reply is still owed — had no test, and the implementation of it was
right only because I had thought about it while writing the code. A green
suite said nothing about it.

This is the commit-first discipline paying for itself in a way I did not
expect. The discipline exists so an interrupted stage leaves a green
commit. What it also does is make "what am I *not* sure about?" a cheap
question to ask, because the code is already safe to look at. Under the
old order (code → close-out → commit), the same gap would have been found
by the same person in the same place; but if the session had been killed
before the close-out, the gap would have shipped with the code and no
record of having been considered. The commit-first order turned an
unrecorded gap into a recorded one — that is the entire difference, and
it is worth a stage to have learned it.

The second finding is the one the stage is actually about: **a protocol
needs a rule for where a message ends, and the rule has to be chosen
before the buffer is sized.** The "consume only complete messages" shape
makes the buffer a constant; the obvious shape makes it a function of
the peer. Both shapes work. Only one of them bounds the server.

## 7. What was left on the table, deliberately

- **`poll()` instead of `select()`.** `select()` caps at `FD_SETSIZE`
  (1024 descriptors on Linux) and builds a bitmask by hand; `poll()`
  takes an array and has no ceiling. Same shape, different container.
  `select()` is here because its machinery is visible — you can see the
  bitmask — and the point of this arc is to see the machinery.
- **Idle timeouts.** A connection can sit open forever. A real server
  needs a deadline; this one needs a clock it does not have.
- **`SO_REUSEPORT`** — several processes sharing one port. Not needed
  while there is one process.
- **Multiple threads.** One process, one thread, many connections. The
  concurrency ladder the design doc rejected as "orthogonal to I/O" is
  still orthogonal to I/O.
- **HTTP.** That is 65.6, and it is the first consumer of this framing
  layer.

## 8. Provenance: the discipline, second use

This stage ran under the commit-first order adopted after Stage 65.3
was orphaned by the 900-second judgment timeout: **tests → valgrind →
commit → close-out → wiki**, with the commit as the point of no return.

It worked. The code landed at `9828192` with 17/18 of the suite green
and valgrind clean; the missing test was then added and committed
separately (`253419a`) rather than folded back into the first commit.
Two commits instead of one is the right trade: the first commit is a
complete, verified, shippable stage, and the second is an addition to
it. Squashing them would have meant rewriting a commit that was already
good.

The whole stage — design, implementation, 18 tests, valgrind across five
binaries, two commits, this document and the wiki updates — fit inside a
single judgment pass, which is the first stage to do so. The earlier
ones needed more because they were each learning something the arc had
not paid for yet.

## 9. Next

**Stage 65.6 — HTTP/1.1 server from the wire: request line, headers,
`Content-Length`, keep-alive, 404.** The first protocol built on the
framing layer, and the first place where the framing has to be right,
because HTTP's framing rules are *textual* rather than length-prefixed:
you find the end of the headers at `\r\n\r\n` and the end of the body at
a `Content-Length` you have to trust. A byte stream does not know where
anything is; HTTP is a set of conventions for finding out.

### What would change the pick

Tom can redirect the target in one message — the pick is mine, the aim is
his. Concretely: a steer toward DNS (65.8) instead of HTTP; a steer back
to the browser's one genuinely open item (the active-formatting-elements
list and the `<a>` special case); or a request to close the sockets arc
at 65.5. The thing that would *not* change the pick is silence — the
ladder is mine to walk, and 65.6 is the next rung Tom has already seen
described.
