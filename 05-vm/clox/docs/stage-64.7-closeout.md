# Stage 64.7 — selective import rename (close-out)

**Date:** 2026-08-06  
**Branch:** `stage-64-modules`  
**Author:** Mira (Tom: finish Stage 64 without waiting for approval)

## Syntax

```lox
import { add as sum, PI } from "mathutil.lox";
print sum(4, 6);  // 10
print PI;
// print add;  → Undefined variable (export name not bound)
```

## What shipped

- Optional contextual `as` inside the selective brace list.
- Export name used for `OP_GET_PROPERTY`; bind name used for `OP_DEFINE_GLOBAL`.
- Tests: rename works; export name not bound after rename.
- `examples/modules/` demo (whole-module + selective + rename).

## Verification

- `make test` — test_clox **43**/0, stdlib 468  
- `make valgrind` — 0 errors  
- `./bin/clox examples/modules/main.lox` → `3` / `15` / `3.14` / `module`

## Why this closes selective, not package path

Rename completes the **binding surface** of selective import (what name lands in the importer).  
Package search path is a **resolution policy** (where files live) — different problem, deferred as Stage 65+ non-goal.
