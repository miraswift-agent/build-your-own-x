# Browser harden slice 7b-2a — The inner loop's prerequisites, measured (close-out)

**Date:** 2026-11-11 (CST night) · **Branch:** `browser-harden-honesty`
**Follows:** slice 7b-1 (`acafc15`, the oracle) · **Precedes:** slice 7b-2 (the port)

No code changed in this slice. This is the measurement that re-scopes 7b-2.

---

## 1. The lesson

**The oracle told me the gap was one case. The spec told me the case was
the wrong one to trust.**

Slice 7b-1 said: "the only difference from html5lib is `block_between`,
the furthest-block path; port that." That was right about the *symptom*
and wrong about the *cause*, and it took reading the spec text — not the
spec's memory — to find out why.

## 2. What the gap is actually made of

`<b>1<p>2</b>3</p>` — ours `<b>1<p>23</b>`, html5lib `<b>1</b><p><b>2</b>3</p>`.

html5lib's answer has the `<b>` **closed at the block boundary** and
**re-opened inside the `<p>`**, with the stray text inside the paragraph.
Ours leaves the `<b>` open across the `<p>` and swallows the `</b>`.

The experiment (temporarily removing the `p` marker from the list of
active formatting elements) did **not** close the gap — the gap set was
unchanged. The reason is structural, and it is what 7b-2 has to port:
html5lib's answer is produced by the inner loop's *clone-and-move*
machinery — it creates a **new** element for the formatting element and
**reparents** the old one's children into it, under the common ancestor.
Nothing this component does today can produce that shape, because it has
no reparenting primitive at all.

So 7b-2 is confirmed as the right pick, and its real size is now known:
**the port is not a fix to `adoption_agency`; it is a new primitive
(create a clone, reparent children, insert at a position) plus the
algorithm on top of it.**

## 3. The bigger finding: the marker rules have moved

Reading the current WHATWG spec (2026) text rather than recalling it
turned up a divergence this component has been carrying since slice 6.

**§13.2.6 (spec, today):** "The markers are inserted when entering
**applet, object, marquee, template, td, th, and caption** elements."

**The spec's "insert a marker" rules — there are exactly four:**
template; applet/marquee/object; caption; th/td.

**html5lib 1.1's source:** exactly three `activeFormattingElements.append(Marker)`
calls — the same three places (applet/marquee/object, caption, th/td).

**Slice 6 implemented:** `applet | marquee | object | button | p`.

So slice 6's marker-push set matches **neither** the current spec nor
html5lib 1.1:

| element | spec (2026) | html5lib 1.1 | slice 6 |
|---|---|---|---|
| applet, marquee, object | marker | marker | marker ✅ |
| template, td, th, caption | marker | marker | **missing** ❌ |
| button | no | no | **marker** ❌ |
| p | no | no | **marker** ❌ |

(`td`, `th` and `caption` also *clear* the list back to the last marker,
which slice 6 does implement — the spec's table rules do both: clear,
then insert a marker.)

I do not yet know what the *right* answer is for this component, and
that is a question about which spec version the component targets, not a
detail. It is recorded here as an open item, not silently patched.

## 4. What 7b-2 now needs

1. **DOM primitives**: create an element for a token; reparent a node's
   children into another node; insert a node at a chosen position in a
   parent.
2. **The algorithm**: the inner loop of §13.2.8.3 — the furthest block,
   the common ancestor, the bookmark, the clone-and-move steps, and the
   inner-loop counter's cap of three.
3. **A decision on the marker set** (item 3 above) before the port can be
   trusted, because the marker bounds the search that the port performs.

## 5. Scope, stated plainly

No code changed. The oracle from 7b-1 is unchanged and still green
(403 executions across the component, 0 failed). `block_between` still
differs from html5lib — deliberately, and now with the reason written
down.

## 6. Next

**Slice 7b-2 — the port**, preceded by the marker-set decision, which is
now a named piece of work rather than an assumption.

### What would change the pick

Tom can redirect this in one message: back to the clox ladder, into the C
sockets component's open items (chunked transfer-encoding, `Host`
routing, TLS, `SIGPIPE`), onto the Vercel publish gate, or toward the
browser's non-correctness platform item. Silence does not change it.
