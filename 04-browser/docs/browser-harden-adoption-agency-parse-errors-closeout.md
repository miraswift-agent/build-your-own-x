# Browser harden slice 7c — The adoption agency's parse errors (close-out)

**Date:** 2026-11-12 (CST morning) · **Branch:** `browser-harden-honesty`
**Predecessor:** slice 7b-2c (`35ad136`, the furthest-block path)

---

## 1. What shipped

The outer loop already knew *where* the spec's parse errors live (steps
7, 8, 9). What was missing was that they were **recorded**. They are
now, with the spec's own codes:

| code | when |
|---|---|
| `adoption-agency-1.2` | the formatting element is not in the stack of open elements |
| `adoption-agency-1.3` | the formatting element is not the current node |
| `adoption-agency-4.4` | the formatting element is not in scope |
| `unexpected-end-tag` | the "any other end tag" fallback ran into a special element |
| `unexpected-start-tag-implies-end-tag` | a nested `<a>` start tag closes the active anchor |

The format is **`code: sentence`**. The codes are html5lib's — which are
the spec's — so the component's error list is now directly comparable
with the oracle's `parser.errors` instead of being prose only.

The expectations in `tests/stage07c_test.rs` were produced by html5lib
1.1 on the same markup (`HTMLParser().parse(...)`, then reading
`parser.errors`), **not transcribed from the spec prose**. My memory of
the spec's error codes has been wrong before, twice.

**No tree changes** — the error list is metadata, and the oracle test
from slice 7b-1 still passes unchanged.

**421 test executions across the component, 0 failed** (378 unique): six
new integration tests, two new in-module tests, and two existing
in-module tests extended with the error assertions.

## 2. Two findings worth keeping

**The oracle's unreachable branch is not the component's bug.**
html5lib's `adoption-agency-4.4` branch is dead code: the "not in
scope" test is folded into the branch that acts as "any other end tag",
so html5lib can never record 4.4. This component follows the spec and
records it.

**The component and the oracle can only be compared where they model the
same thing.** `<b>x<table><td>y</b>`: html5lib clears the formatting
list (its table insertion mode creates an implied `<tbody>`, and a
`<tbody>` start tag clears the list), so it records
`unexpected-end-tag`; this component has **no table insertion modes**,
so the `<b>` survives on the list and reaches the out-of-scope branch
instead. Same markup, different subsystems — a comparison that ignores
the subsystem boundary is a comparison of two different parsers.

**And a third, found while looking for a markup route to 4.4:** every
markup route I tried (caption, object, applet, marquee, `td`, template)
landed on `unexpected-end-tag` instead — those tags either clear the
formatting list or push a **marker**, and a marker bounds the search for
a formatting element. Only `<table>` reached 4.4, because a table start
tag does not clear the list. *A markup route to a branch can be closed
by a different subsystem*, which is worth knowing before writing a test
that claims to exercise it.

## 3. The lesson

**A parse error is a claim about the input, so the code should be the
claim.** The component's error list was sentences only — "nested `<a>`
start tag: the active anchor is closed" — which is readable but not
comparable, not greppable against the spec, and not checkable against
the oracle. With the code first and the sentence second, the list
becomes a vocabulary shared with html5lib.

The second lesson is the one I keep re-learning: **the oracle is a
runtime, not a library of expectations.** Running html5lib on the same
markup and reading `parser.errors` is how I found the dead branch, the
table-mode divergence, and the marker that closed the route to 4.4. My
memory of the spec's error codes would have produced a test that passes
and proves nothing.

## 4. Scope, stated plainly

- **No trees change.** This slice records metadata.
- The `adoption-agency-1.2` branch is reachable only by construction in
  this component (every markup route to it is closed by the AFE
  clearing or by a marker); it is covered by the in-module test.
- The outer loop's eight-iteration cap still aborts silently, as
  html5lib does; the spec says to act as "any other end tag" there, and
  that difference is left for a later slice rather than folded in here.

## 5. Next

**The browser's remaining open item — the repository branch state.** It
is a platform question rather than a correctness one: `master` is 61
stages behind `stage-64-modules`, there is no `main` branch, and the
browser work lives in a second worktree on `browser-harden-honesty`.
The first move is a written decision memo — what each branch holds,
what each option costs — because the irreversible half (fast-forwarding
`master`, adopting `main`) is a layout call.

### What would change the pick

Tom can redirect this in one message. A steer back to the clox ladder; a
steer to the C sockets component's deferred items (chunked
transfer-encoding, `Host` routing, `Date:`, TLS, `SIGPIPE`); a request
to spend the time on the Vercel publish gate instead of the ladder; or
a steer about the branch layout itself would each move this pick aside.
Silence would not: the board says what is open, and walking to the next
item is my job.
