# Browser harden slice 7b-2c — The furthest-block path of the inner loop (close-out)

**Date:** 2026-11-12 (CST morning) · **Branch:** `browser-harden-honesty`
**Predecessors:** slice 7b-1 (`acafc15`, the oracle), slice 7b-2a
(`25c1211`, the prerequisites), slice 7b-2b (`cdeb63d`, the primitive)

---

## 1. What shipped

The adoption agency's inner step now has **two paths**.

**The furthest-block path (new).** When a *special* element sits at or
above the formatting element in the stack of open elements — that is,
when a block interrupted the formatting element and is still open — the
formatting element is **cloned**, the block's children are **reparented**
into the clone, and the clone is **re-opened inside the block**. The
formatting survives the block boundary.

**The no-furthest-block path (unchanged).** The component's existing
behaviour: pop the stack to the target and re-open the misnested
formatting elements above it.

The port is html5lib 1.1's `endTagFormatting`, **read from the
oracle's source** (`html5parser.py:1404–1566`), not from the spec's
prose. Steps 5 through 15 are implemented; step 10's table
foster-parenting is not (this component has no table insertion modes,
so that branch cannot be reached).

## 2. The tree that changed

`<b>1<p>2</b>3</p>`

| | body |
|---|---|
| before | `<b>1<p>23</b>` |
| after | `<b>1</b><p><b>2</b>3` |

which is html5lib 1.1's tree for that markup — the case that slice 7b-1
measured as the one and only gap.

**The known-gap constant is deleted, not edited.** `KNOWN_GAPS` is gone
from `tests/stage07b_test.rs`; the test is now
`the_inner_loop_matches_html5lib_in_every_case` and asserts that every
case matches html5lib 1.1.

**411 test executions across the component, 0 failed** (370 unique).

## 3. The lessons

**A port's blast radius is a property you can MEASURE before you write
it.** Of the six cases in slice 7b-1's fixture, only `block_between` has
a furthest block; the other five take the existing path, which was
already correct against html5lib. Checking which path each case takes
*before* writing the code is what made this slice safe to land in one
sitting — and it is a check, not a guess.

**Read the oracle's algorithm, not its output.** I had the spec's prose
in my head — furthest block, common ancestor, bookmark — and I was about
to implement from it. The source showed three things my memory had
wrong: html5lib's step 5 searches from the formatting element *at or
above* it (not strictly below), step 6 pops without re-opening, and
step 10 foster-parents only for tables. My memory of the spec had been
wrong before, twice. It was wrong again here, and the source caught it
again.

**The outer loop's second pass is not a detail — it is the closing of
the clone.** Without that pass the clone stays open and the text after
the stray end tag lands inside it: `<b>23</b>` instead of html5lib's
`<b>2</b>3`. My first trace of the algorithm got this wrong and the
trace caught it. The outer loop exists precisely because the inner loop
leaves work behind.

**A gap you can name is a gap you can close.** Slice 7b-1 said one case
differed. Slice 7b-2b built the verb it needed. Slice 7b-2c used it.
Three slices, one gap, closed — and the oracle that named the gap was
the same oracle that adjudicated the fix.

## 4. Scope, stated plainly

- The **no-furthest-block path is unchanged** — it was already correct
  against html5lib for the five fixture cases that take it.
- The `<a>` start-tag case is unaffected: with no open block, it takes
  the existing path. Its call site now passes the anchor's id.
- **Table foster-parenting is not implemented** — this component has no
  table insertion modes, so the branch is unreachable.
- The mutation-recording decision from slice 7b-2b stands: the adoption
  agency's rewiring is parser-internal surgery and does not record
  mutation records.

## 5. Next

**Slice 7c — the adoption agency's parse errors.** The outer loop
already knows where the spec's parse errors live (steps 5, 7, 8, 9);
what is missing is that they are *recorded* — the component's parser
collects errors, and the adoption agency's errors are the ones a reader
would want to see. That is the surface slice 7a deliberately left alone.

### What would change the pick

Tom can redirect this in one message. A steer back to the clox ladder; a
steer to the C sockets component's open items (chunked transfer-encoding,
`Host` routing, `Date:`, TLS, `SIGPIPE`); a request to spend the time
on the Vercel publish gate instead of the ladder; or a steer toward the
browser's other open item — which is not a correctness item but a
platform question — would each move this pick aside. Silence would not:
the board says what is open, and walking to the next item is my job.
