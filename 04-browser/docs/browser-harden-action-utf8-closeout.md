# Browser-harden close-out — Action stubs honesty + non-UTF8 bodies

**Date:** 2026-07-21  
**Branch:** `browser-harden-honesty`  
**Scope:** remainder slice after commits `6b55720` / `fe6b291` / `a32a6c7`

## What shipped

1. **Action honesty (`src/agent/action.rs`)**
   - `Scroll` success string tagged `[scroll:stub]` — no viewport/layout model.
   - `Hover` requires a matching element (was always-Ok), then tags `[hover:stub]` (no pointer events / `:hover` CSS).
   - `Evaluate` returns `Err` on `Page::execute` — refuses silent stub success; points callers at CDP/CLI eval.
   - Docs on the `Action` enum state the real / stub / not-wired contract.

2. **Non-UTF8 response bodies (`src/net/http.rs`)**
   - New `body_text() -> Result<&str, String>` — strict UTF-8; error includes byte offset, length, content-type.
   - New `body_as_str_lossy()` — U+FFFD replacement; never empty-on-error.
   - `body_as_str()` now aliases `body_text()` (breaking the old `unwrap_or("")` footgun that made binary look like an empty page).
   - Call sites in `page.rs` / `main.rs` / stage03 tests updated.

3. **Page mutex poison (`src/agent/page.rs`)**
   - `lock_doc` / `lock_doc_mut` map poison → `Err` instead of panicking on the action/nav paths used above.

## Tests

- `cargo test` green end-to-end on host with `~/.cargo/bin` on PATH.
- New unit tests: valid UTF-8, invalid UTF-8 rejects (not empty), binary content-type in error.
- Renamed/strengthened action tests: scroll admits stub; hover requires element + admits stub; Evaluate errs.
- `#[test]` / `#[tokio::test]` attribute count in tree: **328**.

## Still open (not this commit)

- Broader unwrap cleanup in `query.rs` / `element.rs` / `extract.rs` (still `.lock().unwrap()`).
- Parser gaps / adoption agency.
- Full agent-layer mutex discipline beyond page+action paths.

## Lesson

Partial substrates lie when success strings look complete. Tag stubs in the message, and refuse APIs that cannot do the work (`Evaluate`). Same shape as "Built ≠ Shipped": a green `Ok` that did nothing is worse than an honest `Err`.
