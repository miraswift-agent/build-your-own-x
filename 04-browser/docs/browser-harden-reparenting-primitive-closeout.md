# Browser harden slice 7b-2b — The reparenting primitive (close-out)

**Date:** 2026-11-11 (CST night) · **Branch:** `browser-harden-honesty`
**Predecessor:** slice 7b-2a `25c1211` (the inner loop's prerequisites,
measured)

---

## 1. Why this slice

Slice 7b-2a measured the `block_between` gap and found it structural:
html5lib's answer is produced by the inner loop's **clone-and-move**
machinery, and this component has **no reparenting primitive at all**.
The port was therefore two pieces — a primitive, and an algorithm on top
of it. The primitive is the load-bearing piece: the algorithm cannot be
written until the tree can be rewritten.

## 2. What landed

Three operations on `Document`, in `src/html/dom.rs`:

| operation | what it does | html5lib's name |
|---|---|---|
| `clone_element(id)` | a fresh element with the same tag name, attributes, category and role — and **no children** | "an element for the token for which the node was created" |
| `reparent_children(from, to)` | move every child of `from` to the end of `to`; the children keep their identity and their text, their parent changes | `reparentChildren` |
| `insert_child_at(parent, child, index)` | insert an existing node at a position, detaching it from its previous parent first; an index past the end appends | the "insert last node into node, first removing it from its previous parent" step |

Three unit tests: a clone keeps its name and attributes but starts
empty; reparenting moves the children and empties the old parent;
insert-at reorders the siblings and clamps an index past the end.

**411 test executions across the component, 0 failed** (370 unique).

## 3. The lesson

**A tree is a set of pointers, and a parser's recovery rules need to
*move* things, not just add them.**

Until this slice the component could only append: every element went to
the end of the current node, and the only way to change a tree was to
add to it. The adoption agency's inner loop is a *tree-rewriting*
algorithm — it clones an element, moves children across, and inserts at a
position. None of those three verbs existed. An algorithm you cannot
express is a design, not an implementation, so the verbs come first and
the algorithm comes next.

**The second lesson: the oracle's source is the port plan.** Slice 7b-2a
named the missing primitive in prose — "create a clone, reparent
children, insert at a position". html5lib's own tree builder confirms
that set *and supplies the vocabulary*: `reparentChildren` is its name
for the move. Reading the oracle's source is how I knew the primitive
list was complete rather than plausible. (It is also how slice 6b caught
the marker set: four lines of `grep`.)

## 4. The scope, stated plainly

**The primitive changes no tree yet** — the parser does not call it.
This is an API slice, the same shape as slice 4 (list bookkeeping): the
work is invisible until something uses it. `block_between` still differs
from html5lib, and the known-gap set is still exactly `{block_between}`.

The mutation-recording question was decided and written down: these
operations are parser-internal tree surgery and do **not** record
mutation records, because the adoption agency's rewiring is the parser
fixing its own tree, not a change an outside observer caused. If the
component's mutation semantics ever change, this decision is the one to
revisit first.

## 5. Next

**Slice 7b-2c — the port itself**, on top of these three verbs: the
furthest block, the common ancestor, the clone-and-move of the inner
loop, and the list bookkeeping that goes with it. Verified against slice
7b-1's fixture — done when the known-gap set is **empty** and the
`KNOWN_GAPS` constant has been deleted rather than edited.

Then **slice 7c — the adoption agency's parse errors**, the surface
slice 7a deliberately left alone.

### What would change the pick

Tom can redirect this in one message. A steer back to the clox ladder, a
steer to the C sockets component's open items (chunked transfer-encoding,
`Host` routing, TLS, `SIGPIPE`), a request to spend the time on the
Vercel publish gate instead of the ladder, or a steer toward the
browser's other open item — which is not a correctness item but a
platform question — would each move this pick aside. Silence would not:
the board says what is open, and walking to the next item is my job.
