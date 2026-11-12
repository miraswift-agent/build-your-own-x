# Browser harden, slice 3 — the list of active formatting elements and the `<a>` special case (close-out)

**Date:** 2026-11-11 (CST evening) · **Branch:** `browser-harden-honesty` · **Commit:** `8a28c10`
**Component:** `04-browser` (Rust) · **Predecessors:** slice 1 `0934ff4` (hermetic fixtures), slice 2 `b7f8f36` (adoption agency re-opens misnested formatting elements)

---

## 1. The lesson

**The parser's recovery rules are a *list*, not a stack.**

The stack of open elements answers "what is open right now". The list
of active formatting elements answers a different question: "what is
still *in scope* for the formatting rules". For most of the tree
construction algorithm the two coincide, which is why the stack is such
a convenient stand-in — and why the `<a>` rule is the first place the
difference becomes visible.

The rule itself (§13.2.6.4.7) is short: *a start tag for `<a>` does not
nest. If an `<a>` is still in the list of active formatting elements, it
is closed first — a parse error — and the two anchors end up as
siblings.*

```
<p><a href="a">1<a href="b">2</p>
  becomes
<p><a href="a">1</a><a href="b">2</a></p>
```

Before this slice the parser had no list at all, so it nested the
anchors: `<a href="a">1<a href="b">2</a></a>`.

## 2. What changed

`04-browser/src/html/tree_builder.rs` gained the list of active
formatting elements (§13.2.6), maintained in three places:

- **`insert_and_push`** — the single choke point every element creation
  goes through. When a new element is created and the list is not
  empty, a **marker** is pushed; if the element is a formatting
  element, it joins the list too.
- **`pop_until` / `pop_until_any` / `pop`** — when an element leaves the
  stack of open elements, it leaves the list.
- **`find_active_anchor`** — the `<a>` search, and the special case
  itself in the "in body" insertion mode.

### The marker is the whole point

The search for an active `<a>` stops at the **last marker**. A marker
is pushed whenever a new element is created while the list is
non-empty, so the search never reaches past the most recent
non-formatting element. That is what makes the rule *bounded*:
formatting that has been separated by an ordinary element is out of
scope, and a second `<a>` over there does not close the first.

The ordering is load-bearing and was the part I had to get right by
reading rather than by intuition: the marker is pushed when the element
is **created**, and the formatting element joins the list **after**
that. Push them the other way round and every `<a>` immediately
follows its own marker, the search finds nothing, and the rule never
fires.

## 3. The oracle, and the route that worked

The expected tree was cross-checked against **html5lib 1.1**, not
transcribed from the spec prose — slice 2's first expectation was wrong
and the reference caught it, so that discipline is not optional.

The system python is **externally managed** (PEP 668) and refuses
`pip install html5lib` outright; `parse5` is not in the site's
`node_modules`. What worked:

```bash
python3 -m venv /tmp/oracle && /tmp/oracle/bin/pip install html5lib
```

A throwaway venv, no system-python surgery, no `--break-system-packages`
flag on a machine I do not own. Worth writing down: the obvious
install command is the one that fails, and the working one is a
directory away.

## 4. The findings

### 4.1 A fixture with two cases in it couples them

The first fixture draft held both cases I wanted:

```html
<p><a href="a">1<a href="b">2</p>
<p><b><a href="a">3<a href="b">4</p>
```

html5lib's tree for that file was not two clean paragraphs. It was:

```
<p><a href="a">1</a><a href="b">2</a></p><a href="b">
</a><p><a href="b"><b></b></a><b><a href="a">3</a><a href="b">4</a></b></p>
```

The second paragraph's case was dragged across the first one by the
tree builder's **end-of-file** handling: the open formatting elements
left over at EOF are reconstructed, and that reconstruction reaches back
across the paragraph boundary. The two cases were not independent, and
neither the expected tree nor the test would have been honest about
what was being measured.

**One case per fixture.** Block-separated cases in one file still
couple through EOF. The fixture now holds the single clean case, and
the tree it asserts is the one html5lib actually produces.

### 4.2 The AFE list is not enough — the adoption agency has to use it

The trees for **marker-crossing** cases still differ from html5lib's,
because the adoption agency in this parser still takes its "outer list"
from the open stack rather than from the list of active formatting
elements. In html5lib, `<a href="a">1<div><a href="b">2</a></div>`
closes the outer anchor before the `<div>` and reconstructs an empty
anchor inside it; this parser leaves the outer anchor open across the
`<div>`.

That is not a bug in the slice — it is the **boundary of the slice**,
and the code says so. The adoption agency rewrite is the next slice,
not this one. But it is worth naming, because a list that exists and
is not consulted by the algorithm that needs it is a list that will
rot: the next person will assume the adoption agency uses it.

### 4.3 A stack cannot answer a scope question

The general form of the lesson, which is the one worth carrying out of
this component: **a stack is the wrong data structure for a scope
question.** "Is this `<a>` still in scope for the formatting rules?"
cannot be answered by looking at what is open, because the answer
depends on *markers* — boundaries recorded at the moment an element was
created. The stack knows what is open; it does not know where the
scopes are. Building the list is what turns the question from
unanswerable into a scan.

## 5. The tests

One new test, `parse_nested_anchor_special_case`, with its own named
fixture `tests/fixtures/16_nested_anchor.html`. It asserts:

- the paragraph's tree is `<a href="a">1</a><a href="b">2</a>` — the
  html5lib-verified shape;
- there are exactly two anchors;
- the first anchor holds only `"1"` — if the parser nested them, it
  would hold `1<a href="b">2</a>`.

**347 unique tests, 0 failed** (365 executions across nine binaries —
one more execution and one more unique test than slice 2's 346/364).

Valgrind: **N/A** — pure Rust crate; the memory story is the borrow
checker's, and the compiler is the tool for it.

## 6. What was left on the table, deliberately

- **Noah's Ark clause** — the three-same-name limit on the list.
- **The adoption agency's outer list** — it must come from the list of
  active formatting elements, not from the open stack (§4.2). This is
  the next slice.
- **The reconstruction at EOF** — leftover formatting elements at the
  end of the document are reconstructed by the adoption agency, which
  is where the coupling in §4.1 comes from.
- **Attributes on list entries.** The spec's list entries carry the
  element's attributes so the agency can reconstruct them; this
  implementation keeps only the name and the marker, and reads
  attributes from the DOM when it re-opens an element.

## 7. Next

**Browser harden slice 4 — the adoption agency takes its outer list
from the list of active formatting elements, plus the Noah's Ark
clause.** It is the same component, the same worktree, the same branch:
the list exists now, and the algorithm that needs it is the one that
has to learn to read it.

### What would change the pick

Tom can redirect in one message — the pick is mine, the aim is his. Any
of these moves slice 4 aside: a steer to close the browser remainder
entirely (the parser is a *practical subset* and always will be, and
the agent-facing API does not care about `<a>` nesting); a steer to
return to the BYOX ladder proper (the clox VM, where the last work was
Stage 63's `array_flat_map`); a steer to open a new C arc in the
sockets component's style; or a request to spend the time on the
Vercel publish gate instead of the ladder. What would **not** change
the pick is silence.
