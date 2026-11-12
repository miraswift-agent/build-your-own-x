# Browser harden slice 6 — The marker semantics (close-out)

**Date:** 2026-11-11 (CST evening) · **Branch:** `browser-harden-honesty`
**Predecessor:** slice 5 `47ad8c5` (the adoption agency's outer loop)

---

## 1. The lesson

**A marker is a boundary, not a fence — and the worst marker is one
everywhere.**

The list of active formatting elements is bounded by markers: a search
for "the element with the same tag name" stops at the last marker,
because formatting that was separated from the end of the list by a
marker is out of scope for the formatting rules.

This component pushed a marker before **every** element. So the search
was bounded at the most recent tag, and the scope the marker exists to
mark was thrown away at every element. The list was technically
correct and practically useless: it could never hold formatting from
before the last element.

The spec's marker list is small and specific (§13.2.6):

| rule | elements | what happens |
|---|---|---|
| push a marker | `applet`, `marquee`, `object`, `button` | a marker is inserted at the end of the list |
| push a marker | `p` (start tag) | same |
| clear the list back to the last marker | `caption`, `col`, `colgroup`, `frame`, `head`, `tbody`, `td`, `tfoot`, `th`, `thead`, `tr` | everything from the end of the list, up to and including the last marker, is removed |

Everything else leaves the list alone. That is the whole point: most
elements do **not** end formatting's scope.

## 2. What changed

- `insert_element` — the one place every element is created — now
  applies the rules: the clear happens *before* the element is created,
  the marker push *after*.
- The unconditional `push_afe_marker()` in `insert_and_push` is gone.
- New helpers: `pushes_marker(tag)`, `clears_list(tag)`,
  `clear_afe_to_last_marker()`.

## 3. The findings

**Two of my expectations were wrong, and the code was right both
times.** The first two runs of the new tests failed, and both failures
were mine:

- the marker occupies an index of its own, so in `<b><button>x</button><i>y`
  the `<i>` is at index **2**, not 1 — the marker is at 1;
- "clear the list back to the last marker" removes the marker and
  everything after it, while what came **before** it stays in scope, so
  the `<b>` survives the clear (`["b"]`, not `[]`).

**The blast radius was small by luck of the wiring, not by design.**
The list's only tree-facing consumer today is the `<a>` special case;
the adoption agency's outer loop — the thing that would make the marker
matter — is not wired into the tree yet (slice 5 built it as a
control-flow unit with an injected inner step). So this slice fixes the
list without moving any tree. That is convenient and it is also exactly
why the fix could not have been verified against html5lib here: the
marker-crossing trees still differ, and they will differ until the outer
loop is wired. **The safety of this change came from the previous
slice's incompleteness**, which is worth naming rather than hiding.

**A note on the board.** The wiki's shorthand for this slice said "clear
the list back to a marker for `p`/`td`/`th`/`caption`/`template`". The
spec puts `p` on the *marker* side (a `p` start tag pushes a marker)
and does not list `template` at all. This implementation follows the
spec, because the oracle is html5lib and the spec, not my own
paraphrase of them. (html5lib's `clearToLastMarker` takes the *first*
marker rather than the last — a divergence from the spec's words that is
worth remembering when the trees are compared.)

## 4. The scope, stated plainly

The marker rules are wired and the list now holds the right scope. The
tree does not change for marker-crossing documents yet, and this slice
does not claim it does. **The gap to html5lib remains, and it is now
narrower: the list is right, and the outer loop that consults it is
still not wired in.**

## 5. Tests

5 new unit tests (in-module, as `src/` already carries `#[cfg(test)]`
modules):

- a plain element does not end the formatting scope (`<b><div>` keeps the
  `<b>` in the list);
- `button` pushes a marker;
- `p` pushes a marker;
- the marker bounds the search (the `<b>` is out of reach, the `<i>` is
  not);
- clearing removes everything up to and including the last marker, and
  keeps what came before it.

The existing suites are untouched and green: **396 test executions
across the component, 0 failed.**

## 6. Next

**Slice 7 — wire the adoption agency's outer loop into the tree.** The
outer loop exists (slice 5) but is not called by the parser. Wiring it
means: an end tag for a formatting element runs the outer loop, which
searches the list for the element (bounded by the last marker), and
then either runs the inner loop (the re-opening of §13.2.8.3) or
aborts with a parse error.

This is where the marker fix finally shows up in a tree, so the
discipline has to be the one that worked in slices 1–3: **a named
marker-crossing fixture, its expected tree cross-checked against
html5lib 1.1, and the pre-fix tree verified before the change** so the
test genuinely fails first.

### What would change the pick

Tom can redirect the target in one message; the pick is mine, the aim
is his. A steer back to the clox ladder, a steer to the C sockets
component's open items (chunked transfer-encoding, `Host` routing, TLS,
`SIGPIPE`), a request to spend the time on the Vercel publish gate, or
a steer toward the browser's other open item — which is not a
correctness item at all but a platform question — would each move this
pick aside. What would **not** change it is silence: the board says
what is open, and walking to the next item is my job.
