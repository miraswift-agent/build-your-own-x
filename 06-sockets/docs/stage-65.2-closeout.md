# Stage 65.2 — addresses by hand (close-out)

**Date:** 2026-11-11
**Branch:** `stage-65-sockets` (byox) — design doc `06-sockets/docs/stage-65-sockets-design.md`
**Status:** COMPLETE — 8/8 new tests pass (16/16 across the component), valgrind clean (0 errors, 0 leaks)

---

## 1. The lesson

**An address is a byte string, not a name.**

Stage 65.1 asked the kernel to turn a name ("127.0.0.1") and a number (a port)
into bytes. Stage 65.2 does that work itself. The endpoint becomes a struct of
bytes we assemble by hand:

```c
typedef struct {
    uint8_t port[2];   /* network byte order: port[0] is the high byte */
    uint8_t ip[4];     /* 127.0.0.1, one byte per octet */
} sock_ipv4_bytes;
```

The point is not to be clever. The point is that an address becomes something you
can *point at and compare*, instead of a string you have to trust a library to
translate. Once the address is bytes, "the kernel stored a different port than we
asked for" is a `memcmp` away from being obvious — and Stage 65.3's fork-per-connection
loop will want exactly that kind of certainty.

### The one exception, pinned as an exception

Not everything in a `sockaddr_in` is a network byte string. POSIX stores
`sin_family` in **host** byte order — it is a number (AF_INET = 2), not bytes.
Write it as network order and the kernel reads 0x0200 and rejects the address.
The test `the_family_field_is_host_order` pins this as the exception it is, so the
next reader does not "fix" it by byte-packing everything.

## 2. What changed

`src/sockets.{c,h}`:

| Added | What it does |
|---|---|
| `sock_ipv4_loopback_bytes` | builds the endpoint by hand: two port bytes, four address bytes |
| `sock_addr_apply` | writes our bytes into a `sockaddr_in` the kernel will accept |
| `sock_addr_extract` | reads a `sockaddr_in` back out as bytes |
| `sock_port_from_bytes` | the port number carried by two network-order bytes |

**Removed:** every library address helper. The component no longer calls the
byte-order converters, the dotted-quad parser, or the name resolver — the
component builds its addresses by hand, and the port we hand back is read out of
the kernel's own bytes rather than converted from a value we assumed.

The `sockaddr_in` port and address fields already hold their bytes in network
order, so the bytes we built are written straight in. **No conversion step, and
no assumption about this machine's byte order** — the code is the same on a
big-endian host.

## 3. Red first

`make test` with the new test file in place:

```
tests/test_stage_65_2.c:61:5: error: unknown type name 'sock_ipv4_bytes'
tests/test_stage_65_2.c:64:9: error: implicit declaration of function 'sock_ipv4_loopback_bytes'
...
```

The weak form of the discipline again — the red is "the thing does not exist,"
not "the thing is wrong." So the suite was mutation-tested, and the mutations
came out very differently. That difference is the interesting part.

## 4. The tests (8)

Every expectation is computed **by hand**. The test never asks the library to
produce the answer it is checking against — that would be checking the
implementation against itself.

1. `loopback_bytes_are_127_0_0_1` — four bytes, not a string.
2. `port_bytes_are_big_endian` — 0x1234 → {0x12, 0x34}.
3. `port_bytes_are_big_endian_across_the_range` — every port from 0 to 65535.
4. `applied_address_carries_our_bytes` — the struct carries the bytes we specified.
5. `the_family_field_is_host_order` — the exception, pinned as an exception.
6. `the_kernel_round_trips_our_bytes` — what we hand the kernel is what it stores.
7. `extracted_bytes_rebuild_the_same_address` — bytes out, bytes back: identity.
8. `the_component_builds_its_addresses_by_hand` — the contract test (see §5).

## 5. Two mutations, two very different results

### Mutation A — the port bytes in the wrong order (a real bug)

`put_port_bytes` was changed to emit the low byte first. **Five tests across
both suites caught it:**

```
65.1:  "accept_returns_a_distinct_fd"                     ✗ sock_connect_loopback failed
65.1:  "echo_round_trip_preserves_bytes"                  ✗ could not establish a connection
65.2:  "port_bytes_are_big_endian"                        ✗ port bytes are not big-endian
65.2:  "port_bytes_are_big_endian_across_the_range"       ✗ some port did not produce big-endian bytes
65.2:  "applied_address_carries_our_bytes"                ✗ the struct's port bytes are not the bytes we specified
```

Worth noticing *which* tests stayed green, and why: tests 6 and 7 are round-trip
and identity tests, so a **consistent** encoding error is invisible to them — the
kernel agrees with us, and we agree with the kernel. That is not a flaw in those
tests; it is what round-trip tests are for. The tests that catch a consistent
error are the ones that assert an *absolute* byte layout (2, 3, 4), and they
did.

### Mutation B — the same bytes, produced by the library (no bug at all)

`put_port_bytes` was left alone, but `sock_addr_apply` was rewritten to convert
the port with the library's byte-order helper instead of writing our bytes
directly. **The output is identical — so all eight behavioural tests stayed
green:**

```
8/8 passed        ← every behavioural test, blind to the change
"the_component_builds_its_addresses_by_hand"   ✗ src/sockets.c still uses htons
7/8 passed
```

This is the reason test 8 exists. **The lesson of this stage lives in the
construction, and the construction is invisible in the output.** No behavioural
test can tell "we built these bytes" from "the library built these bytes for us",
because the bytes are the same. The only thing that can see it is the contract
test, which reads `src/*.c` and refuses the library helpers outright.

It is a source-scanning test, and that is unusual — normally I would distrust
one. Here it is not brittle in the way source-scanning usually is, because it
scans the whole `src/` directory (a rename inside `src/` does not break it) and
because the thing it asserts is exactly the stage's contract. A test that
asserts something about the *implementation* is the right tool when the
implementation *is* the lesson.

## 6. Valgrind

Both binaries: **0 errors, 0 leaks**, "All heap blocks were freed."

## 7. Honest scope — what this stage deliberately does *not* do

- **Only loopback, and only IPv4.** `sock_ipv4_bytes` is four bytes of address.
  IPv6 (16 bytes) and any name resolution are out of scope — DNS is Stage 65.8.
- **No other address.** `sock_ipv4_loopback_bytes` is the only builder. There is
  no "any address" function yet; the point of the stage is the byte handling,
  not the address book.
- **The write is still naive** (65.4), **there is no concurrency** (65.3), and
  **`accept` still blocks** (65.5) — all unchanged from 65.1 and all left on the
  table deliberately.
- **The Makefile changed shape.** The component now follows the house pattern
  (clox's): each `tests/test_*.c` becomes its own binary and `make test` runs
  them all, so a new stage drops in a test file and inherits the runner. The
  65.1 suite still passes unchanged under the new runner — 16/16 across the
  component.

## 8. What would change this pick

- **Tom redirects the arc's target.** One message. The strongest alternative is a
  concurrency ladder; the design doc says so explicitly. The pick is mine, the aim
  is his.
- **A browser spec example fails.** The active-formatting-elements list and the
  `<a>` special case in 04-browser are still unimplemented; a failing spec example
  outranks a new component.
- **Tom's visible problems move.** The Vercel publish gate, the xAI token, the
  content backlog are his and outrank my curriculum.
- **65.3–65.4 prove too easy.** If the next two stages are an hour of `man` pages
  rather than stages, the ladder gets reshaped before it goes further.

## 9. Next

**Stage 65.3 — the accept loop with `fork()` per connection.** One process accepts,
one child per connection, the child serves and exits, the parent reaps. The
lesson: *a server is a loop, and the loop is where the concurrency lives.* The
piece this stage has to get right is the one the fd lifecycle made visible — the
child inherits the listener, so the parent must not close it, and the child must
close the listener it does not need, or the socket never drains.
