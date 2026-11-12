# Browser harden slice 5 — The adoption agency's outer loop (close-out)

**Date:** 2026-11-11 (CST evening) · **Branch:** `browser-harden-honesty`
**Commit:** `47ad8c5` · **Predecessor:** slice 4 `f8b7f6e` (the Noah's Ark clause)

---

## 1. The lesson

**The outer loop is where the list and the stack argue — and the inner
loop is what happens after they settle.**

The adoption agency algorithm (§13.2.6.4) has two halves, and the
component had only ever implemented the second one. The inner loop is
the part that moves nodes around; the outer loop is the part that
decides *which* element it is going to work on, and it is where the
spec's parse errors live. Three ways the two structures can disagree:

| the list says | the stack says | what the spec does |
|---|---|---|
| there is a `<b>` there | it is not in the stack | parse error; **remove it from the list** and abort |
| there is a `<b>` there | it is in the stack, but out of scope | parse error; abort |
| there is no `<b>` there | — | parse error; abort, and act as "any other end tag" |

And the **marker is the boundary**: the search for the subject runs
backwards from the end of the list and stops at the last marker, because
formatting that was separated from the end of the list by a
non-formatting element is out of scope for the formatting rules.

The search itself is the one line that matters most in this slice:

```rust
for i in (0..self.afe.len()).rev() {
    if self.afe[i].marker {
        return None;   // the last marker bounds the search
    }
    if self.afe[i].name == tag {
        return Some(i);
    }
}
```

## 2. What changed

- `OuterOutcome` — the outer loop's five outcomes: `InnerCompleted`,
  `NoSuchElement`, `NotInOpenStack`, `NotInScope`, `IterationLimit`.
- `find_formatting_element(tag)` — the search above.
- `adoption_agency_outer(tag, inner)` — the loop: the **eight-iteration
  cap** (step 5), the search (step 7), the not-on-the-stack case (step
  8), the out-of-scope case (step 9), and the inner step.

The inner step is **injected as a closure**, and that is the design
decision worth keeping: the inner step reports whether it made progress,
and the loop runs again when it did. The spec's outer loop keeps going
because the *full* inner loop re-inserts the formatting element into the
list, which gives the next iteration something new to find. With the
inner step behind a closure, that contract is explicit instead of
implicit, and the loop's control flow can be tested with no tree at all.

## 3. The finding: the markers are wrong, and that is the blocker

**This component pushes a marker onto the list before EVERY element
insertion.** The spec does not do that: it pushes a marker for a
specific set of elements (`applet`, `marquee`, `object`, `button`) and
*clears the list back to a marker* for others (`p`, `td`, `th`,
`caption`, `template`). The current rule makes the list look like it
has far more boundaries than it really has, and it is exactly why
**marker-crossing trees still differ from html5lib's** — the `<b>` a
`<p>` separated from the end of the list is behind a marker that should
never have been there, so the list-driven search cannot find it.

That is why this slice stops at the list. **The outer loop is not wired
into the tree yet**, and it should not be: wiring it in before the
markers are right would replace a stack-driven search that is right in
the common case with a list-driven search that is wrong in a different
way. The markers come first; the next slice fixes them against
html5lib 1.1 and then wires the loop in.

## 4. Tests

6 new unit tests, all list-level:

- the search finds the **last** matching entry;
- the search **stops at the last marker** (a `<b>` behind a marker is
  not found; an `<i>` in front of it is);
- an element that is **not on the stack** is removed from the list;
- an element that is **out of scope** aborts and leaves the list alone;
- the loop **stops at exactly eight iterations** (an inner step that
  always reports progress still cannot run forever);
- a finished inner step ends the loop after one iteration.

The existing suites are untouched and green: **386 test executions
across the component, 0 failed** (358 unique: 347 + slice 4's 5 + these
6).

## 5. Next

**Slice 6 — the marker semantics, and the outer loop wired into the
tree.** Two pieces, in this order:

1. Push markers where the spec pushes them (`applet`, `marquee`,
   `object`, `button`) and clear the list back to a marker where the
   spec clears it (`p`, `td`, `th`, `caption`, `template`) — checked
   against **html5lib 1.1**, not against my reading of the spec.
2. Then wire `adoption_agency_outer` into the tree, with a named
   fixture for a marker-crossing tree (`<b>1<p>2</b>3</p>`) whose
   expected tree comes from html5lib, and the pre-fix tree verified
   before the change so the test fails first.

### What would change the pick

Tom can redirect the target in one message; the pick is mine, the aim is
his. A steer back to the clox ladder, to the C sockets component's open
items (chunked transfer-encoding, `Host` routing, TLS, `SIGPIPE`), to
the Vercel publish gate, or toward the browser's other open item — the
platform question, not a correctness one — would each move this pick
aside. What would **not** change it is silence: the board says what is
open, and walking to the next item is my job.
