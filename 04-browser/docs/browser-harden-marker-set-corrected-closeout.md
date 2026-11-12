# Browser harden slice 6b — The marker set, corrected (close-out)

**Date:** 2026-11-11 (CST night) · **Branch:** `browser-harden-honesty`
**Predecessor:** slice 6 `e9859b4` (the marker semantics — the set was
wrong), slice 7b-2a `25c1211` (the finding that prompted the check)

---

## 1. The lesson

**An oracle you claim to have checked must be checked against the
source, not against your memory of it.**

Slice 6 shipped the marker set `applet / marquee / object / button / p`
and its close-out claimed the set was cross-checked against html5lib
1.1. The claim was false. The set came from the spec's prose as I
remembered it, and the cross-check was never made. The check takes four
lines of `grep`:

```
html5parser.py:1139  startTagAppletMarqueeObject -> append(Marker)
html5parser.py:1740  startTagCaption             -> append(Marker)
html5parser.py:2150  startTagTableCell           -> append(Marker)
```

Three marker sites in the whole library. `startTagButton` has none, and
no `p` start tag inserts one anywhere. So slice 6 pushed markers for two
tags the spec does not, and **one existing test depended on the wrong
answer** — it used a `<button>` as the marker that bounded a search.

The second lesson is the one the sibling's 7b-2a close-out pointed at:
**a marker set is a spec-version question.** The 2026 WHATWG spec and
html5lib 1.1 agree with each other here and both disagree with slice 6.
Slice 6 matched neither.

## 2. The corrected set

The spec's in-body rules name four tags: **applet, marquee, object,
template**.

| tag | slice 6 | corrected | evidence |
|---|---|---|---|
| `applet` | marker | marker | spec + html5lib (`startTagAppletMarqueeObject`) |
| `marquee` | marker | marker | spec + html5lib (same site) |
| `object` | marker | marker | spec + html5lib (same site) |
| `template` | — | **marker** | spec only — html5lib 1.1 has no template rule (it predates it) |
| `button` | marker | **none** | html5lib's `startTagButton` inserts no marker; the spec does not either |
| `p` | marker | **none** | a `p` start tag closes an open `p` in button scope; it does not mark the end of formatting's scope |

### Deliberately out of scope: `caption`, `td`, `th`

The spec and html5lib agree that these push markers — html5lib's
`startTagCaption` and `startTagTableCell` are two of its three marker
sites. But those rules live in the **table, caption and cell insertion
modes**, and this component's `InsertionMode` has nine variants
(`Initial`, `BeforeHtml`, `BeforeHead`, `InHead`, `AfterHead`, `InBody`,
`Text`, `AfterBody`, `AfterAfterHtml`) and **no table modes at all**.
Applying those marker rules through the generic `insert_element` choke
point would push markers in body contexts where the spec does not.
Recorded as an open item, not silently patched — the same discipline the
7b-2a close-out used.

## 3. The consequence in the tests

`the_fallback_stops_at_a_special_element` used a `<button>` as the
marker that bounded the outer loop's search, and asserted the fallback
path gave up at the special element. With the corrected set a `<button>`
no longer bounds anything, so the test now uses `<object>`, which does
push a marker.

The button case's **outcome changes**: `NoSuchElement` becomes
`InnerCompleted`. That is the *more* spec-correct answer, not a
regression — a `<button>` is not a scope boundary, so `</b>` should find
the `<b>` in the list and run the adoption agency rather than fall
through to the generic end-tag path. A correction that changes a
test's expected value is a correction that was worth making; a
correction that changes nothing is a correction nobody checked.

## 4. What did not move

**The oracle's gap set is unchanged: exactly `{block_between}`.** The
other five named cases still match html5lib, and `block_between` still
differs (ours `<b>1<p>23</b>`, html5lib `<b>1</b><p><b>2</b>3`).

So the marker-set correction is a real correctness fix that does not
move this fixture. Both of those are worth saying plainly: the fix
stands on its own evidence (the source), and the fixture is not yet
sensitive enough to show it.

## 5. The scope, stated plainly

- The adoption agency's inner loop is still not implemented. `block_between`
  still differs, and the port still needs the **reparenting primitive**
  that 7b-2a identified.
- The table-mode marker rules (`caption`, `td`, `th`) are not applied,
  because this component has no table modes.
- The `template` marker is spec-only; html5lib 1.1 cannot confirm it,
  so the component's oracle is silent on that one tag.

**405 test executions across the component, 0 failed** (367 unique).

## 6. Next

**Slice 7b-2 — the port of the furthest-block path**, on a correct
marker set: the reparenting primitive (clone an element, move its
children, insert at a position) and then the inner loop's steps on top
of it — furthest block, common ancestor, close the formatting element at
the block boundary, re-open it inside the block. Done when the known-gap
set is empty.

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
