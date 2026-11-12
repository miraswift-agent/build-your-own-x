# Browser harden slice 4 — The Noah's Ark clause (close-out)

**Date:** 2026-11-11 (CST evening) · **Branch:** `browser-harden-honesty`
**Commit:** `f8b7f6e` · **Predecessor:** slice 3 `8a28c10` (the list of
active formatting elements + the `<a>` special case)

---

## 1. The lesson

**The list's entries are not names — they are elements, and the spec
compares them as elements.**

The Noah's Ark clause (§13.2.6) says: whenever a formatting element is
inserted and pushed onto the list of active formatting elements, *if the
list contains three elements with the same tag name, namespace,
attributes and text, remove the earliest one.*

Two of each animal. That is the clause's name and its arithmetic: the
list ends up holding **two** identical entries, never three.

Slice 3 gave the list entries that were a `name: String` and a
`marker: bool`. That is enough to find "an `<a>` between the end of the
list and the last marker" — the `<a>` special case — and not enough for
this clause, because the clause compares **attributes** and **text**, and
an entry that is only a name cannot answer either.

So each entry now carries the element it refers to (`id: Option<NodeId>`,
`None` for markers), and the comparison asks the document what the
element actually is.

## 2. What changed

- `AfeEntry` gains `id: Option<NodeId>`.
- `insert_and_push` runs the clause after pushing a formatting element.
- `noahs_ark(tag, id)` — collects the entries with the same tag name
  that are *identical* (name + attributes + text) and removes the
  earliest of them when there are three.
- `same_formatting(a, b)` — the identity comparison: same attributes,
  same text content. Namespaces are not modelled in this component, so
  the comparison is name (checked by the caller) + attributes + text.

## 3. The two consequences that surprised me

**The clause only fires for elements that are still empty.** The text of
an element changes as the parser fills it, and the comparison is made at
the moment a *new* element is inserted. So:

| input | list afterwards | why |
|---|---|---|
| `<b><b><b><b>` | 2 × `<b>` | all four identical and empty → the earliest goes, twice |
| `<b>1<b>2<b>3<b>4` | 4 × `<b>` | by the fourth `<b>` the first three hold different text |
| `<b><b class="x"><b><b>` | 3 × `<b>` | the first, third and fourth are identical → the earliest of those goes; the `<b class="x">` is never a candidate |
| `<div><div><div><div>` | — | `<div>` is not a formatting element, so it never enters the list |

**Attributes are part of the identity.** The third row is the test that
distinguishes a correct implementation from a sloppy one: an
implementation that compared names alone would collapse all four and
leave two.

## 4. The finding

**My first expectation was wrong, and it was wrong in an instructive
way.** I wrote the first test expecting three survivors — "the list
keeps three, and the earliest is the one that goes" — which is a
perfectly reasonable misreading of the clause's wording, and the test
failed with two. The spec's wording ("contains three elements with the
same ... then remove the earliest one") leaves **two** identical entries
behind. The clause's name says so: Noah's Ark carried two of each
animal. My expectation encoded "three" where the spec says "two", and
the code — which implemented the spec — was right.

The second finding is about method rather than code: one test's actual
value disagreed between two runs of the same input. Rather than reason
about it, I dumped the list's entries — name, element id, attributes,
text — and the arithmetic followed immediately from the dump (the
`<b class="x">` was in the list with its attribute attached, and the
first `<b>` was the one that had been removed). **When a value
disagrees with itself, dump the state; do not argue with it.**

## 5. The scope, stated plainly

**This slice does not change the tree.** The clause is list bookkeeping,
and the list is internal state; it becomes observable in the tree only
when the adoption agency consults the list, which is the next slice.
The tests are therefore unit tests on the list itself, not tree
comparisons against html5lib — a weaker guarantee than slices 1–3
gave, and honest about it. The unit tests live in the module (the
component already carries `#[cfg(test)]` modules in `src/`).

## 6. Tests

5 new unit tests (the clause fires and leaves two; it does not fire
when the text differs; it compares attributes; identical attributes
count as identical; non-formatting elements never enter the list).

The existing suites are untouched and green: **374 test executions
across the component, 0 failed.**

## 7. Next

**Slice 5 — the adoption agency's outer loop.** The adoption agency
currently finds its target by scanning the *stack* of open elements,
which is what slice 2 did. The spec (§13.2.6.4) instead searches the
*list of active formatting elements*, bounded by the last marker, and
then:

- if the subject is in the list but **not in the stack of open
  elements**, it is a parse error: remove it from the list and abort;
- if it is in the stack but **not in scope**, it is a parse error: abort;
- the outer loop runs at most **eight** times.

That is where this slice's clause becomes observable: with the list
driving the search, the oldest identical entry is the one the agency
never finds, and a tree comparison can finally show it.

### What would change the pick

Tom can redirect the target in one message; the pick is mine, the aim
is his. A steer back to the clox ladder, a steer to the C sockets
component's open items (chunked transfer-encoding, `Host` routing, TLS,
`SIGPIPE`), a request to spend the time on the Vercel publish gate, or
a steer toward the browser's other open item — which is not a
correctness item at all but a platform question — would each move this
pick aside. What would **not** change it is silence: the board says
what is open, and walking to the next item is my job.
