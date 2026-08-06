# Stage 64.6 — selective import (close-out)

**Date:** 2026-08-06  
**Branch:** `stage-64-modules`  
**Author:** Mira (Tom: work on BYOX selective import)

## Syntax

```lox
import { add, PI } from "mathutil.lox";
print add(2, 3);
print PI;
```

Whole-module form unchanged:

```lox
import "mathutil.lox" as math;
```

- Top-level only (same rule as 64.2).
- Contextual `from` (identifier, not a new keyword).
- Empty `{}` is a compile error.
- Missing export → runtime `Undefined property '…'.`
- Names not listed are **not** bound (no silent dump).
- Shares `vm.modules` cache with whole-module import of the same path.

## Implementation

1. **`OP_DUP`** — duplicate TOS (bind loop needs the module under each get).
2. **Compiler** — after `import`, if `{` … parse name list, `from`, path string; emit:
   - `OP_IMPORT path`
   - per name: `OP_DUP` · `OP_GET_PROPERTY name` · `OP_DEFINE_GLOBAL name`
   - `OP_POP` (drop module)
3. Path string parsing shared via `importPathConstantFromPrevious()`.

No new module object model. Selective import is sugar over load + field gets + defines.

## Tests (`test_clox` 41 total, +6)

- basic bind + call/var  
- unbound name not imported  
- missing export  
- empty list compile error  
- nested import still rejected  
- selective + `as` share cache (`add == m.add`)

## Verification

- `make test` — test_clox 41/0, composability 8, repl 5, stdlib 468  
- `make valgrind` — 0 errors, allocs=frees on all four binaries  
- Manual: `examples/selective-import.lox` → `15` / `3.14`

## Lesson

Namespace import teaches “who owns the module object.” Selective import teaches “what crosses the boundary.” Same load graph; different binding surface. `OP_DUP` is the smallest VM hook that makes multi-bind honest without a special-case opcode.

## Non-goals (still later)

- `import { a as b }` rename  
- `export` keyword / private tops  
- Package search path  

## Files

`chunk.h`, `vm.c`, `debug.c`, `compiler.c`, `tests/test_clox.c`,  
`examples/selective-import.lox`, `examples/sel_math.lox`, this close-out.
