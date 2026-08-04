# Browser-harden close-out — query/element/extract lock unwraps

**Date:** 2026-08-04  
**Branch:** `browser-harden-honesty`  
**Scope:** remainder slice after Action/UTF-8 (`56a317f`)

## What shipped

1. **`query.rs`** — all `self.doc.lock().unwrap()` paths now use `Page::lock_doc()`.  
   - `Option` APIs (`query`, `query_input`, `query_link`): poison → `None`.  
   - `Vec` APIs (`query_all`, `query_role`, `query_text`): poison → empty vec.  
   No panic on poisoned DOM mutex.

2. **`element.rs`** — `Element::lock_doc()` helper (same poison → `Err` string as `Page`).  
   - Readers (`text_content`, `inner_html`, attrs, class_list, selectors): empty/`None` on poison.  
   - Writers (`type_text`, `set_attribute`): no-op + note on poison.  
   - `is_visible` / `is_enabled`: **fail closed** (`false`) on poison.

3. **`extract.rs`** — extract_* use `lock_doc()`.  
   - Tables/links/forms/text: empty on poison.  
   - Metadata: empty `PageMetadata`.  
   - `extract_structured`: JSON includes `"error": "page DOM mutex poisoned"` so callers can tell silence from empty page.

Matches the honesty pattern already on action/nav in `page.rs` (`56a317f` era).

## Tests

- `cargo test` green end-to-end (`~/.cargo/bin` on PATH).
- No new failing suites; existing agent/dom/html/net coverage still passes.

## Still open (parser / agency)

- Broader parser gaps / adoption agency (not this slice).
- Other `.unwrap()` outside query/element/extract (DOM internals, tests) — out of scope unless they surface on agent API.

## Why this is education not vanity

Poison panics made agent APIs lie about failure mode (crash vs empty). Empty/`None`/fail-closed is the honest partial substrate the rest of browser-harden already chose.
