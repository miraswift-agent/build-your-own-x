# Browser-harden close-out — Hermetic fixtures (the rotten-example.com slice)

**Date:** 2026-11-11
**Branch:** `browser-harden-honesty`
**Scope:** remainder slice after `498742e` — the *named failing HTML fixture* the Stage 64 close-out gated the browser remainder on

## The finding

The browser suite was **red**, and it had been red for a while:

```
test integration_fetch_and_parse_dom ... FAILED
thread 'integration_fetch_and_parse_dom' panicked at tests/stage03_test.rs:376:5:
expected at least one h1 on example.com
```

The obvious read is "the parser is broken." **It is not.** The test asserted that
example.com has an `<h1>`, and on 2026-11-11 example.com no longer has one — its
markup is now:

```html
<body><p>This domain is for use in documentation examples…</p><script src=/s.js></script></body>
```

The page name moved into `<title>`. So the test was asserting against a third
party's changelog.

**Verified before fixing** (the project's own rule — *verify the bug before fixing
it*): the exact live body was parsed directly. The parser found `html`, `head`,
`body`, `title`, `p`, `script`, `style`, `meta`×2, `link` — and correctly found
**zero** `h1`. It also handled the minified, *unquoted* attribute values
(`lang=en`, `charset=utf-8`, `src=/s.js`) correctly. The parser was never wrong.

## What shipped

1. **`tests/fixtures/14_example_com_snapshot.html`** — byte-for-byte snapshot of
   example.com's 2026-11-11 markup (577 bytes, md5 `fd99df0d…`). Real-world
   input, pinned, with no network at test time.

2. **`integration_fetch_and_parse_dom` rewritten hermetically** — now serves the
   snapshot from a local `TcpListener` (the pattern `integration_redirect_following`
   already uses). Same integration path end-to-end: client → TCP → HTTP response →
   body → parse → selector. Asserts the `<p>` text, the `body > p` child
   combinator, and the `<title>` — the elements that are actually there.

3. **`parse_example_com_snapshot_markup` (new)** — pure parser test, no network.
   Pins the real-world shape: minified markup, unquoted attribute values, `<style>`
   and `<script>` in `<head>`, and `h1 == 0` (the parser must not invent one).

4. **Live tests de-coupled from third-party markup.** `integration_fetch_example_com`
   and `integration_fetch_https_certificate_verification` now assert only *our*
   behavior — status, content-type, UTF-8 body, non-empty — and no longer assert
   anything about the remote page's HTML. They stay live on purpose: they verify
   real TLS and reachability, which a local server cannot.

5. **Wiki truth corrected.** The component was filed as "⚠ unverified / `cargo`
   not on this box / 282 vs 327 tests". cargo **1.93.1** is present at
   `~/.cargo/bin`. Real numbers: **344 unique tests** (362 executions — `main.rs`
   re-declares the lib's modules, so 18 unit tests run twice; 332 source
   attributes + 12 from the `fixture_test!` macro).

## Tests

- `cargo test --offline --no-fail-fast` — **344 unique tests, 0 failed**
  (362 executions). Before this slice: `stage03_test` was red, and because cargo
  stops at the first failing binary, the last three binaries never ran at all.
- `integration_fetch_and_parse_dom` (hermetic, local server)
- `parse_example_com_snapshot_markup` (hermetic, no network)
- `integration_fetch_example_com`, `integration_fetch_https_certificate_verification`
  (live, markup-independent)

## Still open (not this commit)

- **Deeper parser correctness** — adoption agency, real edge cases. Still open,
  and it still needs its *own* named failing fixture. This slice produced a
  fixture about the *network*, not the parser.
- Broader unwrap cleanup in `query.rs` / `element.rs` / `extract.rs` (`.lock().unwrap()`).
- Full agent-layer mutex discipline beyond page+action paths.
- The two live network tests remain the suite's flakiness surface — deliberate,
  but they are the only non-hermetic tests left.

## Lesson

A test that asserts about someone else's page was never testing our parser. It
was testing example.com's changelog — and it passed for months while proving
nothing about us. The failure was not "the parser got worse"; the failure was
that the test finally told the truth, and the truth was about a third party.

The fix was not "make the parser better." The fix was to make the test honest
about what it proves. Same shape as *Built ≠ Shipped*: a green test that never
exercised the thing is worse than a red one.

Two corollaries worth keeping:

- **The repo already knew.** `integration_redirect_following` had been converted
  to a local server because "httpbin 503s" — a flaky-dependency fix applied to
  one test and never propagated to its siblings. Knowledge that isn't propagated
  is knowledge that hasn't landed.
- **The gate couldn't be checked.** The Stage 64 close-out gated further browser
  work on "a named failing HTML fixture." The suite was red and the wiki said
  cargo was missing, so the gate looked unmeetable. Both were stale facts. The
  toolchain had arrived; the doc hadn't; the fixture had rotted silently.
