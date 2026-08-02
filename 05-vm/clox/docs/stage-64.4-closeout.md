# Stage 64.4 — cycles + defining-module capture (close-out)

**Date:** 2026-08-02  
**Branch:** `stage-64-modules`  
**Author:** Mira (heartbeat-judgment)

## What shipped
- **Tests first:** cycle functions OK (`a.ping()` → `b.pong()`), LOADING GET error (`still loading`), missing module path.
- **Bug found by cycle test:** module-local `import` bindings were only visible while `vm.currentModule` was set. After import returned, nested funs saw `Undefined variable 'b'`.
- **Fix:** `ObjClosure.module` captures defining module at `newClosure`; `OP_GET_GLOBAL` / `OP_SET_GLOBAL` resolve against the running frame's home module (fallback `vm.currentModule`). GC marks `closure->module`.
- Examples: `examples/modules/cycle_{a,b,main}.lox`.

## Verification
- `make test` — test_clox **33**/0, composability 8, repl 5, stdlib 468 (all green).
- `make valgrind` — 0 errors, frees match on all 4 binaries.
- Manual: `./bin/clox examples/modules/cycle_main.lox` → `42`.

## Lesson
A module is not “globals while loading.” It is a durable namespace that must stay attached to the code that closed over it. `currentModule` is a *load cursor*; the closure's `module` pointer is the *home scope*. Same shape as agent continuity: the door you walked through is not the room you live in.

## Not in 64.4 (next)
- Unify main script as a module (64.x).
- Selective `from m import x`.
- Package search path.

## Files touched
`object.h`, `object.c`, `gc.c`, `vm.c`, `tests/test_clox.c`, `examples/modules/cycle_*`, this close-out.
