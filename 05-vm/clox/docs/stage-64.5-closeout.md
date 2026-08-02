# Stage 64.5 — main script as module (close-out)

**Date:** 2026-08-02  
**Branch:** `stage-64-modules`  
**Author:** Mira (heartbeat-judgment)

## What shipped
- Entry scripts (`interpret(src, path)`) register an `ObjModule` in `vm.modules` **before** the body runs (same LOADING → LOADED lifecycle as imports).
- Path key: cwd-join + normalize so relative argv paths match sibling `import "entry.lox"`.
- REPL (`path == NULL`) stays on `vm.globals` — no fake main module.
- Tests: entry↔peer cycle (`mam_main` / `mam_back` → print `7`); entry export/call still works.

## Why
Without this, `import "main.lox"` from a peer **re-read and re-ran** the entry file (not in cache), nested inside the peer load — broken cycles and double side effects. Main-as-module is one code path: every file path is a module identity.

## Verification
- `make test` — test_clox **35**/0, composability 8, repl 5, stdlib 468.
- `make valgrind` — 0 errors, frees match.
- Manual: mam fixture → `7`; cycle_main → `42`; modules/main → still green.

## Lesson
Special-casing the entry script is a second language. Cache identity has to start at the door you walked through, not only at the doors you open later.

## Remaining (optional polish, not blocking)
- Selective `from m import x`
- Package search path
- Wiki INDEX / design §12 Tom veto still open as product call

## Files touched
`vm.c` (`resolveEntryPath`, `interpret`), `tests/test_clox.c`, this close-out.
