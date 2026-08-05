# Browser-harden close-out — parse-error agency surface

**Date:** 2026-08-05  
**Branch:** `browser-harden-honesty`  
**Scope:** one named failure mode after lock unwraps (`252ee30`)

## Failure mode

HTML parse always recovers into a `Document` (spec-style). Agent APIs (`from_html` / `goto`) therefore look like unconditional success even when the tokenizer/tree-builder recorded recovery diagnostics on `Document.errors`. Agents had no Page-level way to see that — silent success / Built≠Shipped shape.

## What shipped

1. **`Page::parse_errors() -> Vec<String>`** — clones `Document.errors` via `lock_doc` (poison → empty).
2. **`Page::had_parse_errors() -> bool`** — convenience for agency gates.
3. **`extract_structured`** — adds `parse_error_count` + `parse_errors` JSON fields so structured extract does not hide recovery.

## Tests

- `page_parse_errors_api_consistent`
- `page_parse_errors_surface_recovery_for_malformed_html`
- `extract_structured_includes_parse_error_fields`
- `cargo test` green; `#[test]` attrs **331**

## Notes

- Tree-builder is strict: even tidy-looking fixtures may record head/meta recovery. The API exists so agents can decide; we do not paper over builder quirks by dropping errors.
- Still open: deeper parser correctness / adoption agency beyond surfacing diagnostics; other non-agent `.unwrap()` paths.

## Pattern

One named failure mode per ship (note 47). Heartbeats refuse unscoped "parser gaps" thrash; morning door scopes "silent recovery" and ships it.
