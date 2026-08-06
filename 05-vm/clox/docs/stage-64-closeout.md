# Stage 64 — Modules (COMPLETE)

**Date:** 2026-08-06  
**Branch:** `stage-64-modules`  
**Status:** **DONE** — education goals met; no Tom gate remaining on this stage.

## What Stage 64 is

Multi-file Lox: path-resolved imports, module objects, load cache, cycle policy, main-as-module, selective bind (+ rename).

| Slice | Deliverable |
|-------|-------------|
| 64.0 | `interpret(src, path)` + script path for resolution |
| 64.1 | `ObjModule`, `vm.modules` cache |
| 64.2–64.3 | `import "path" as name;` + `OP_IMPORT` |
| 64.4 | Cycles: LOADING + closure home module |
| 64.5 | Entry script is a module (no double-run) |
| 64.6 | `import { a, b } from "path";` + `OP_DUP` |
| 64.7 | `import { a as b } from "path";` |

Design: `stage-64-modules-design.md`  
Slice close-outs: `stage-64.2` … `stage-64.7-closeout.md`

## Surface (final)

```lox
import "mathutil.lox" as math;
import { add as sum, PI } from "mathutil.lox";

print math.add(1, 2);
print sum(10, 5);
print PI;
print typeof(math);  // "module"
```

## Rules frozen for v1

- Top-level imports only  
- Path strings relative to **importer directory**  
- All top-level bindings exported (no `export` keyword)  
- Same path → same `ObjModule` (cache before execute)  
- Cycle: functions OK; reading unfinished peer field → loud runtime error  
- Natives stay on process globals  

## Explicit non-goals (not Stage 64)

- Package / search path registry  
- `export` / private tops  
- Disk bytecode cache  
- Sandboxed import paths  

## Verification (close)

- test_clox **43** passed  
- test_stdlib 468, composability 8, repl 5  
- valgrind clean on all four binaries  
- Demo: `examples/modules/main.lox`

## Curriculum lesson

Single-global VM was a convenience lie. Modules force: path identity, cache-before-run (cycle safety), and a clear binding surface (namespace vs selective vs rename). Same shapes as agent graphs: register before execute; name what crosses the boundary.

## Next BYOX (after 64)

Not another array_* mirror. Candidates when curriculum continues:

- Sideways systems bite (retrospective B), or  
- Package search path if multi-dir projects become real, or  
- Browser-harden with a **named failing HTML fixture** only  

Stage 64 itself is closed.
