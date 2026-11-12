# Browser harden slice 7a — The adoption agency, wired into the tree (close-out)

**Date:** 2026-11-11 (CST evening) · **Branch:** `browser-harden-honesty`
**Predecessor:** slice 6 `e9859b4` (the marker semantics)

---

## 1. The lesson

**A rule that is implemented but not called is a rule the tree does not
have.** `adoption_agency_outer` (slice 5) was complete: the list-driven
search, the parse errors, the eight-iteration cap. It was called by
nothing. The end-tag path still called the old `adoption_agency`, which
searched the **stack** and never consulted the list of active
formatting elements — so the list, the markers, and slice 5's careful
outer loop were all decoration on a parser that did something else.

Wiring it needed two things the component did not have:

- the spec's **"any other end tag"** fallback. When the outer loop
  finds no formatting element with that name, §13.2.8 says to act as
  described in the "any other end tag" entry: walk the stack from the
  top, and **give up at a special element** (`button`, `template`,
  `table`, …). The old path popped straight through them.
- the list removal by **element identity**, not by index. The inner
  step re-opens elements, and re-opening can shrink the list — the
  Noah's Ark clause fires while it does. A stale index removed the
  wrong entry; the first run of this change panicked on exactly that.

## 2. What changed

- `adoption_agency_wired(tag)` — the outer loop as the tree runs it.
  The inner step is this component's re-opening of the misnested
  formatting elements.
- `any_other_end_tag(tag)` — the spec's fallback, with the
  **special-elements list** (`SPECIAL_ELEMENTS`).
- `adoption_agency_outer` — when the inner step completes, the
  formatting element leaves the list (§13.2.8).
- The end-tag path for formatting elements calls the wired version.
  The `<a>` **start-tag** special case still calls the old path; that is
  slice 7b's business.

## 3. The findings

**The tree changes in two places, both toward the spec:**

- an element that is in the list and on the stack but **out of scope**
  is left open (parse error, return) instead of being popped;
- an end tag that matches nothing above a **special element** pops
  nothing, where the old path popped through it.

**The parse-error surface is deliberately unchanged.** This slice
changes what the parser *does*, not what it *says*: the adoption
agency's errors are their own piece of work, and adding them here
would have changed the error list under fixtures that already assert
on it. Saying so is cheaper than a surprise later.

**Two failures on the way, both instructive.** The first run panicked
— an index into the list that the inner step had already invalidated.
The second found my three new tests nested *inside* the previous slice's
last test function, where the harness does not collect them: the count
said 34 before and after. A test that does not run is not a test, and
the count is how you notice.

## 4. The scope, stated plainly

The outer loop is now what the tree runs. The **inner** step is still
this component's simplified re-opening (slice 2), not the full §13.2.8.3
algorithm (furthest block, common ancestor, last node to reproduce), so
the trees still differ from html5lib's in the misnested cases. That is
slice 7b. The error surface is slice 7c.

## 5. Tests

3 new unit tests: the formatting element leaves the list when the inner
step completes; an out-of-scope element is left open; the fallback
stops at a special element. The existing suite is untouched and green:
**396 test executions across the component, 0 failed.**

## 6. Next

**Slice 7b — the full §13.2.8.3 inner loop**, which is where the trees
change: the furthest block, the common ancestor, the last node to
reproduce. It gets the named fixture and the html5lib 1.1 cross-check,
and the pre-fix tree is verified first so the test genuinely fails
before the change. Then **7c** — the adoption agency's parse errors.

### What would change the pick

Tom can redirect the target in one message; the pick is mine, the aim
is his. A steer back to the clox ladder, a steer to the C sockets
component's open items (chunked transfer-encoding, `Host` routing,
TLS, `SIGPIPE`), a request to spend the time on the Vercel publish gate
instead of the ladder, or a steer toward the browser's other open item
— which is not a correctness item at all but a platform question —
would each move this pick aside. What would **not** change it is
silence: the board says what is open, and walking to the next item is
my job.
