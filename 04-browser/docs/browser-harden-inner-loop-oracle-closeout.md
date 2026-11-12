# Browser harden slice 7b-1 — The inner loop's oracle (close-out)

**Date:** 2026-11-11 (CST night) · **Branch:** `browser-harden-honesty`
**Predecessor:** slice 7a `c442655` (the outer loop, wired into the tree)

---

## 1. The lesson

**A gap you can name is a gap you can close. A gap you can only describe
is a backlog.**

The wiki's slice 7b bundled two things: the port of §13.2.8.3's inner
loop, and a named fixture cross-checked against html5lib 1.1. The port is
the old-style algorithm — the furthest block, the common ancestor, the
bookmark, the list split, the clone-and-move — and it does not fit in a
judgment window. Shipping it unverified is worse than shipping the
measurement that will verify it.

So this slice is the oracle and the measurement. The work list for the
port is now written down, case by case, instead of living in a wiki
sentence.

## 2. What is here

`tests/fixtures/17_inner_loop.html` — six named cases, one document each:

| case | markup |
|---|---|
| `misnested_b_i` | `<p>1<b>2<i>3</b>4</i>5</p>` |
| `misnested_deeper` | `<p><b>bold <i>both</i></b><i> italic</i> plain</p>` |
| `block_between` | `<b>1<p>2</b>3</p>` |
| `across_block` | `<b><div>x</div></b>` |
| `anchor_special_case` | `<p><a href="x">1<a href="y">2</a>3</a></p>` |
| `nested_same` | `<b>1<i>2<b>3</i>4</b></i>` |

`tests/stage07b_test.rs` — parses each case as its own document and
compares the tree with html5lib 1.1's tree for the same markup. The
expected trees were produced by html5lib itself, not transcribed from the
spec prose.

## 3. The finding

**The gap is exactly one case.**

Five of the six already match html5lib: the misnested `<b><i>` case, the
deeper misnest, the formatting element across a block, the `<a>` special
case, and the nested-same-name case. The component's simplified inner
step is right more often than I expected.

The one that differs is `block_between`:

```
ours:     <b>1<p>23</b>
html5lib: <b>1</b><p><b>2</b>3
```

That case is the **furthest-block path**, and it is exactly where the
spec's machinery earns its keep: when a block element interrupts a
formatting element, the formatting element has to be *closed* at the
block boundary and *re-opened inside* the block, with the text that
followed the stray end tag staying inside it. The simplified inner step
instead leaves the `<b>` open across the `<p>` and swallows the stray
`</b>` into the paragraph.

So the port is now precisely scoped: **the furthest-block path of the
inner loop** — the furthest block, the common ancestor, the re-opening
inside the block, and the list bookkeeping that goes with it.

## 4. The test as a to-do list

The test asserts that the set of cases differing from html5lib is
*exactly* `KNOWN_GAPS`. Today that is `["block_between"]`. When the port
lands, this test **fails** until `KNOWN_GAPS` is updated — which is the
point. A known-gap test is a to-do list, not a bug report; it also
catches the other direction, a case that was correct and has broken.

## 5. Two serializer normalizations (and a trap)

The comparison needed two normalizations, both about the **serializers**
and not the parsers:

- html5lib's serializer omits optional end tags (`</p>`, `</td>`, …);
  this component's always writes them.
- html5lib leaves attribute values unquoted (`href=x`); this component
  quotes them (`href="x"`).

**The trap:** my first two hand-rolled serializers disagreed with each
other — one put the text `5` *inside* the reconstructed `<i>`, the other
*after* it — and I nearly took the first one's output as html5lib's
truth. The fix was to stop hand-rolling the serializer and use
**html5lib's own** (`html5lib.serialize(doc, tree="etree")`). That is the
third time the reference has caught my memory of the spec, and the second
time a hand-rolled serializer has been the actual bug.

## 6. The scope, stated plainly

The inner loop is **not** implemented in this slice. The tree still
differs from html5lib for `block_between`. What is new is that the
difference is one named case with an expected tree attached, so the port
that fixes it can be verified case by case instead of by memory.

**403 test executions across the component, 0 failed** (one new test
binary; the gap test is the new one).

## 7. Next

**Slice 7b-2 — the port of the furthest-block path.** Concretely: the
inner loop's steps that the measurement says are missing — find the
furthest block, compute the common ancestor, close the formatting element
at the block boundary, re-open it inside the block, and keep the list of
active formatting elements consistent with it. Verified against this
fixture: the port is done when `KNOWN_GAPS` is empty and the test's
`KNOWN_GAPS` constant has been deleted rather than edited.

Then **slice 7c — the adoption agency's parse errors** (the surface slice
7a deliberately left alone).

### What would change the pick

Tom can redirect the target in one message; the pick is mine, the aim is
his. A steer back to the clox ladder, a steer to the C sockets
component's open items (chunked transfer-encoding, `Host` routing, TLS,
`SIGPIPE`), a request to spend the time on the Vercel publish gate
instead of the ladder, or a steer toward the browser's other open item —
which is not a correctness item at all but a platform question — would
each move this pick aside. What would **not** change it is silence: the
board says what is open, and walking to the next item is my job.
