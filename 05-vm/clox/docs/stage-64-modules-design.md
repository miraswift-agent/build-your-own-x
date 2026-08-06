# Stage 64 — Modules design (draft)

**Author:** Mira  
**Date:** 2026-08-01  
**Status:** DESIGN DRAFT — implement only when coherent enough to test  
**Trigger:** Tom autonomy-gap 2026-08-01; self-assessment `05-vm/docs/self-assessment-2026-08-01.md`  
**Veto:** Tom can reject the shape in one message. Absence of this doc was a false gate.

---

## 1. Why modules now (not another array_*)

Stdlib arc (Stages 7–63) taught natives, GC discipline, HO dispatch, multiset algebra.  
What it did **not** teach: multi-file programs, name isolation, load graph, “the language is larger than one buffer.”

Modules are the first *language* feature that is mine rather than Nystrom-plus-lodash.  
Streak brake still applies: **no vanity mirror natives**. Modules are not a vanity mirror.

---

## 2. Goals / non-goals

### Goals (Stage 64 v1)
1. **Import a `.lox` file** from another `.lox` file (and from the main script).
2. **Namespace:** importer sees a single bound name (the module object), not a flat dump into globals.
3. **Cache:** same resolved path loads once per VM lifetime; re-import returns the same object.
4. **Cycles:** deterministic behavior (Python-style partial export preferred over hard error — see §6).
5. **Tests + valgrind** on a small multi-file fixture tree.
6. **One lesson written down:** what multi-file forces in a single-global VM.

### Non-goals (explicitly later)
- Package managers, versioning, `lox_modules/` registry.
- Relative `../` heroics beyond “resolve against importer directory.”
- Selective import — **shipped Stage 64.6** as `import { a, b } from "x.lox";` (still no rename/`as` inside braces).
- Mutable live-binding semantics beyond “module fields are ordinary instance fields.”
- Sandboxing / capability restriction on which paths may load.
- Compiling modules to separate chunks cached on disk.
- REPL `import` edge cases beyond “works if path is absolute or cwd-relative.”

---

## 3. Surface syntax (proposal)

### Preferred form
```lox
import "mathutil.lox" as math;

print math.add(1, 2);
print math.PI;
```

### Selective form (Stage 64.6)
```lox
import { add, PI } from "mathutil.lox";
print add(1, 2);
```

### Alternatives considered
| Form | Pros | Cons | Decision |
|------|------|------|----------|
| `import "path.lox" as name;` | Explicit bind; no silent global pollution; path is a string (matches `io_read_file`) | New keyword `import` + `as` | **v1** |
| `import "path.lox";` bind basename | Less typing | Collision on basename; magic | Reject for v1 |
| `var math = import("path.lox");` expression | Reuses call machinery | Import is not a runtime native call in spirit (compile-time graph + runtime init); harder cycles | Reject for v1 |
| Bare `import math;` | Pretty | Needs module path resolution rules / search path | Later |

**Scanner:** add `TOKEN_IMPORT`, `TOKEN_AS` (or reuse identifier `as` only in import statement context — prefer real keyword `as` only if cheap; else parse `as` as contextual identifier).

**Recommendation:** `TOKEN_IMPORT` keyword; **contextual** `as` (identifier check after import string) to avoid breaking existing code that uses `as` as a variable name.

Grammar (statement):
```
importStmt → "import" STRING "as" IDENTIFIER ";"
```

Imports are **declarations**. They may appear only at top level in v1 (not inside functions). Rationale: simpler compile-time module graph; matches “file is a module.”

---

## 4. Module object model

### New heap type: `OBJ_MODULE` (preferred over reusing `OBJ_INSTANCE`)
```c
typedef struct {
    Obj obj;
    ObjString *name;      /* resolved path or display name */
    Table exports;        /* ObjString* → Value — top-level bindings */
    /* load state for cycles: UNLOADED | LOADING | LOADED */
    int state;
} ObjModule;
```

**Why not bare instance?**  
Classes/instances already mean user OO. Modules are a VM concept (path, cache, load state). Separate type keeps `typeof` honest (`"module"`) and GC/mark paths clear.

**Export rule (v1 — simple):**  
Every **top-level** `var` and `fun` / `class` binding defined while executing the module body is inserted into `module.exports` **and** is readable as `module.name` via `OP_GET_PROPERTY`-like path (or dedicated `OP_GET_MODULE_FIELD`).

Internal-only names: **not** in v1. Everything top-level is exported (Python-without-`__all__`). Document that. A later `export` keyword is optional sugar.

**Natives:** remain on the single VM global table (shared). Module bodies can call `print`, `array_map`, `io_read_file`, etc. without importing a std prelude.

---

## 5. Load algorithm

```
import "rel.lox" as name   in file currently compiling/running as importerPath

1. resolved = resolvePath(dirname(importerPath), rel)
   - If rel is absolute, use as-is.
   - Normalize `.` / `..` enough to make cache keys stable (realpaths optional; prefer lightweight normalize).
2. If vm.modules table has resolved → bind existing ObjModule to `name` in importer scope; done.
3. Else create ObjModule(state=LOADING), insert into vm.modules[resolved] **before** running body
   (critical for cycles).
4. source = readFile(resolved)   // host read, same as main.c readFile — not Lox io_read_file
5. Compile source as TYPE_MODULE (or TYPE_SCRIPT with module context)
6. Run module top-level closure with module’s export table as the write target for globals
7. state = LOADED
8. Bind module object to importer’s `name` (local/global of importer)
```

### Where do module “globals” go?
**Today:** one `vm.globals` for the whole process.  
**v1 change:** while a module body runs, **global get/set** targets that module’s `exports` table (or a `vm.currentModule` pointer). The entry script keeps using `vm.globals` as the “main” module table.

Cleaner long-term: main script is also a module (`"<main>"` or argv path) so one code path.  
**v1 scope:** entry file may stay special (`vm.globals`); imported files use `ObjModule.exports`. Document the asymmetry; Stage 64.1 can unify.

### Compiler interaction
- `compile(source)` → `compile(source, ModuleContext*)`  
- Top-level `var`/`fun` emit stores into module exports when context is a module.  
- `import` statement: either  
  - **A (runtime opcode):** emit `OP_IMPORT` constant(path) + bind local/global, or  
  - **B (compile-time load):** load graph during compile (harder with cycles + runtime side effects).  

**Pick A (runtime import opcode).** Module top-level can have side effects; cycles need runtime LOADING state. Matches Python/JS mental model enough.

---

## 6. Cycles

```
// a.lox
import "b.lox" as b;
fun ping() { return b.pong(); }
var ready = true;

// b.lox
import "a.lox" as a;
fun pong() { return 1; }
// reading a.ready during b's top-level may see nil / incomplete
```

**v1 policy:** allow cycles; while `LOADING`, the module object exists but missing fields read as **runtime error** (“module X not finished initializing”) or return `nil` with a warning print.  

**Prefer runtime error on GET of missing export during LOADING** — fails loud, teaches init order.  
Document: cycle-safe pattern = define functions first (closures resolve later), avoid reading peer vars at top level.

---

## 7. Path rules

- Import strings are **path strings**, not dotted package names.
- Resolution relative to **the importing file’s directory** (not cwd), so fixtures are movable.
- Main script: `dirname(argv[1])`; REPL: cwd.
- Must pass `path` into `interpret` / VM (today `interpret(const char *source)` loses path — **API change required**).

```c
InterpretResult interpret(const char *source, const char *pathOrNull);
```

REPL: `pathOrNull = NULL` → imports resolve vs cwd.

---

## 8. Interaction with existing file I/O natives

| Mechanism | Role |
|-----------|------|
| `io_read_file` / `io_read_lines` | User data; returns strings/arrays; **not** code loading |
| host `readFile` in import | Source load for compile |
| Modules | Code + namespace |

**Do not** implement import as `compile(io_read_file(path))` into current globals — that is `eval` and destroys the lesson.

Security note: import can read any path the process can read. Same trust model as `clox script.lox` and `io_read_file`. No new sandbox in v1.

---

## 9. `typeof` / predicates

- `typeof(m) == "module"`
- Optional later: `is_module(m)` — **not** required for v1 (type-predicate family is complete enough; avoid streak vanity).

---

## 10. Implementation slices (ship as one stage if small enough, else 64a/64b)

| Slice | Work | Tests |
|-------|------|-------|
| **64.0 API** | `interpret(src, path)`; track `vm.currentPath` / main path | existing suite green |
| **64.1 ObjModule + cache table** | type, mark/free, `vm.modules` | unit: newModule, table intern |
| **64.2 Scanner/parser** | `import "…" as x;` top-level only | parse tests / compile error inside fun |
| **64.3 OP_IMPORT + run body** | load, compile, execute, bind | two-file add(); cache identity |
| **64.4 Cycles** | LOADING state + GET rules | a↔b fixture |
| **64.5 Closeout** | docs, wiki INDEX, example `examples/modules/` | valgrind full |

Estimated size: still ~400–700 LOC if careful — larger than a stdlib native, smaller than “rewrite the compiler.”

---

## 11. Test plan (write tests first when implementing)

Fixture layout:
```
tests/modules/
  mathutil.lox          # fun add(a,b) { return a+b; } var PI = 3.14;
  main_import.lox       # import "mathutil.lox" as m; print m.add(2,3);
  cache_a.lox           # side-effect counter file write or global bump via exported var
  cycle_a.lox / cycle_b.lox
  nested/dir/peer.lox   # relative path from nested importer
```

Cases:
1. Basic import + call exported fun  
2. Exported var read  
3. Double import same path → same module identity (`==` if we define module identity by pointer)  
4. Missing file → clear runtime/compile error  
5. Import inside function → compile error  
6. Cycle: functions only → OK  
7. Cycle: read peer var at top level during LOADING → expected error  
8. Relative path from non-cwd importer directory  
9. Valgrind clean on all of the above  

Integrate into `test_stdlib` or new `test_modules` binary — prefer **new binary** so Stage 63 counts stay meaningful.

---

## 12. Open questions (Tom veto surface)

Answer these with “yes / no / change X” — enough to unblock implementation:

1. **Syntax:** `import "path" as name;` OK?  
2. **Cycles:** allow with LOADING errors on early field read — OK?  
3. **Export all top-level** (no `export` keyword) OK for v1?  
4. **Main stays special globals** vs main-is-module unification in the same PR?  
5. Prefer ship as **one Stage 64** or **64a object/cache + 64b syntax/runtime**?

Defaults if no answer (I proceed): **1 yes, 2 yes, 3 yes, 4 main special OK for v1, 5 one stage if tests stay green else split 64a/64b.**

---

## 13. What this teaches *me* (curriculum, not trophy)

- Single global namespace was a **lie of convenience**; multi-file forces “who owns this binding.”
- Cache-before-execute is the same shape as “register before run” in agent graphs (cycle-safe init).
- Path identity is a product decision disguised as filesystem trivia.
- Design-before-code is the antidote to false gates (“waiting for Tom” when Tom was waiting for a draft).

---

## 14. Exit criteria for “design coherent enough to implement”

- [x] Syntax chosen  
- [x] Object model chosen  
- [x] Load + cache + cycle algorithm written  
- [x] API break (`interpret` path) named  
- [x] Test fixtures listed  
- [x] Non-goals listed  
- [x] Tom veto questions ≤5  

**Next action after this file:** either implement 64.0–64.3 on a branch `stage-64-modules`, or wait one beat for Tom veto on §12. Heartbeat/scholar default: **do not wait more than one quiet day** without starting 64.0 if no veto.

---

## 15. Related

- Self-assessment: `05-vm/docs/self-assessment-2026-08-01.md`  
- Long-standing deferral trail: stage 7/8/13/22 closeouts (“modules ~600 lines”)  
- Retrospective A: real Lox feature on top of clox  
- File I/O: Stage 14 / 16 natives — data path only  
