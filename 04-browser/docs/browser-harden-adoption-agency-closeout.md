# Browser-harden close-out — Adoption agency (the misnested-formatting slice)

**Date:** 2026-11-11
**Branch:** `browser-harden-honesty`
**Scope:** browser-harden **slice 2** — deeper parser correctness. Slice 1 produced a
fixture about the *network*; this slice produces the fixture about the *parser* that
the Stage 64 close-out gated the browser remainder on.

## The finding

The tree builder's adoption agency was documented in the source as "simplified":

```rust
fn adoption_agency(&mut self, tag: &str) {
    if self.in_open_stack(tag) {
        self.generate_implied_end_tags(Some(tag));
        self.pop_until(tag);          // ← everything above the target is discarded
    }
}
```

That is wrong for **misnested formatting elements** — and the HTML Living Standard
has an example built for exactly this case (§13.2.8.3):

```html
<p>1<b>2<i>3</b>4</i>5</p>
```

The `</b>` closes `<b>` while an `<i>` is still open inside it. The spec's recovery is
to **re-open** the `<i>` after the `<b>` closes, so the text that follows the stray
end tag stays inside it:

```html
<p>1<b>2<i>3</i></b><i>4</i>5</p>
```

Our parser instead popped the stack to `<b>`, taking the `<i>` with it, and the
italic formatting was **silently lost for the rest of the paragraph**:

```html
<p>1<b>2<i>3</i></b>45</p>          ← what we produced
```

**The damage is invisible in a passing test.** Nothing crashes; the text is all
there. The formatting is just gone. That is why this belonged in a fixture rather
than a note: a parser can be wrong in a way that only shows up in the tree shape.

The named failing fixture — `tests/fixtures/15_misnested_formatting.html` — carries
the spec's example plus a deeper variant where the misnested element carries the
text after the stray end tag:

```html
<p><b>bold <i>both</b> italic</i> plain</p>
```

Red before the fix:

```
assertion `left == right` failed: misnested <i> must be re-opened after </b>
  left: "1<b>2<i>3</i></b>45"
 right: "1<b>2<i>3</i></b><i>4</i>5"
```

## The fix

`adoption_agency()` now:

1. locates the target on the open-elements stack;
2. collects the **formatting elements misnested above it**, with their original
   attributes (`a b big code em font i nobr s small strike strong tt u` — the §13.2.8
   set);
3. closes the target as before (`generate_implied_end_tags` + `pop_until`);
4. **re-opens** the misnested elements as fresh elements (same tag, same attributes)
   so the content that follows the stray end tag lands inside them.

The re-opened element is a *new* node, exactly as the spec requires — the original
`<i>` stays in the tree inside the `<b>`, and the reconstructed `<i>` is its sibling
after it.

**Scope, stated honestly:** this implements the re-opening step for misnested
formatting elements. The full algorithm's list-of-active-formatting-elements
bookkeeping, and its `<a>` special case ("an `<a>` in the list acts as if `</a>` had
been seen"), are still **not** implemented. Non-formatting elements above the target
are still dropped from the stack (they remain in the tree), which matches the spec
for this case.

## The expected trees were cross-checked, not remembered

Both expected trees were produced by **html5lib 1.1** — a spec-compliant HTML5
parsing implementation — on the same markup, installed in a throwaway venv:

```
$ python3 -m venv /tmp/h5venv && /tmp/h5venv/bin/pip install html5lib
$ /tmp/h5venv/bin/python /tmp/h5check.py
'<p>1<b>2<i>3</b>4</i>5</p>'
   html5lib -> <p>1<b>2<i>3</i></b><i>4</i>5</p>
'<p><b>bold <i>both</b> italic</i> plain</p>'
   html5lib -> <p><b>bold <i>both</i></b><i> italic</i> plain</p>
```

The Rust parser now produces byte-identical trees.

**Two verifier bugs, both mine, both caught by checking the reference:**

- My first html5lib run appeared to show *no text at all* in either paragraph. That
  was my serializer, not html5lib: ElementTree stores text in `.text`/`.tail`, not
  as child nodes, and my serializer only walked children. The reference tool was
  fine; my harness was wrong.
- My first Rust expectation for the second paragraph dropped the `<b>` wrapper
  (`"bold <i>both</i></b>…"`), so the parser was "wrong" against a wrong expectation.
  The html5lib cross-check is what caught it — my memory of the spec's output was
  the faulty part, not the parser.

A verifier has to be verified. Twice.

## Tests

- `cargo test --offline --no-fail-fast` — **346 unique tests, 0 failed**
  (364 executions; `main.rs` re-declares the lib's modules, so the 18 unit tests
  execute twice).
- **Baseline measured the same way: 345 unique.** The wiki's prior figure of "344"
  was itself off by one — a copied number rather than a measured one. Measuring it
  is the only reason I know. Now: 346 (this slice adds one).
- New test: `parse_misnested_formatting_adoption_agency` (hermetic, no network).
- New fixture: `tests/fixtures/15_misnested_formatting.html`.
- **Valgrind: not applicable.** 04-browser is pure Rust — there are no `.c`/`.h`
  sources in the crate, and the valgrind step in this project's loop applies to the
  C components (allocator, database, shell). No C code was touched by this slice.

## Still open (not this commit)

- **Full adoption agency** — the list of active formatting elements, and the `<a>`
  special case. The re-opening step is in; the bookkeeping is not.
- **Query / element unwrap cleanup** — `.lock().unwrap()` in `query.rs`,
  `element.rs`, `extract.rs`.
- **Agent-layer mutex discipline** beyond the page + action paths.
- The two live network tests remain the suite's flakiness surface — deliberate,
  but they are the only non-hermetic tests left.

## Lesson

Slice 1's lesson was *a test that asserts about someone else's page is not testing
your parser*. This slice is the mirror: **a parser that passes every test it wrote
for itself may still be wrong in a way nobody wrote a test for.** The gate said
"a named failing fixture" — the honest reading of that gate is *find a case where
we are wrong*, not *write a test around behavior we already had*.

The second lesson is about the reference. It is easy to write the expected tree
from memory of the spec, and my memory was wrong twice (once about the html5lib
output format, once about the expected tree). Pinning the expectation against a
second, independent implementation is what makes the fixture a fact rather than a
recollection — and it is the same discipline as the live-vs-repo website check:
**measure the thing, do not inherit the number.**

Third, smaller: the test count in the wiki had drifted by one because it was
copied forward. Measuring the baseline (345, not 344) cost one `git stash` and
one command, and it caught a number nobody had looked at in three weeks.
