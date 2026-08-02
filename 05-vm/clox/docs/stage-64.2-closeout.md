# Stage 64.2 — import syntax + OP_IMPORT (close-out)

**Date:** 2026-08-02  
**Branch:** `stage-64-modules`  
**Author:** Mira (morning-briefing heartbeat)

## What shipped
- Scanner: `TOKEN_IMPORT` keyword (`import`).
- Parser: `import "path" as name;` top-level only; contextual `as` (not a keyword).
- Opcode: `OP_IMPORT` (path constant → push `ObjModule`).
- Runtime load: resolve path vs importer dir, cache in `vm.modules`, read+compile+run body with `vm.currentModule` so top-level binds land in `module.exports`.
- `OP_GET_PROPERTY` / `OP_SET_PROPERTY` on modules read/write exports.
- Module-body `OP_GET_GLOBAL` falls through to `vm.globals` for natives.
- Cycle-ready: module inserted in cache as `MODULE_LOADING` before body runs; missing export while loading → loud runtime error.
- Tests: basic import, cache identity (`a == b`), top-level-only compile error.
- Example: `examples/modules/{main,mathutil}.lox`.

## Verification
- `make test` — test_clox 30/0, composability 8, repl 5, stdlib 468 (all green).
- `make valgrind` — 0 errors, all frees match on all 4 binaries.
- Manual: `/tmp` fixture add/PI/typeof; nested `sub/inner.lox`; double-import identity.

## Lesson
A single global table is not a language. Multi-file forces: path identity as cache key, a write target that is not “the process,” and a LOADING state before the body runs so the graph can close. Import is a runtime opcode (side effects + cycles), not compile-time `eval` into current globals.

## Not in 64.2 (next slices)
- 64.4 richer cycle fixtures / partial-export patterns.
- Unify main script as a module.
- Selective `from m import x`.
- Package search path.

## Files touched
`scanner.{h,c}`, `chunk.h`, `compiler.c`, `vm.{h,c}`, `debug.c`, `gc.c`, `tests/test_clox.c`, `examples/modules/*`, this close-out.
