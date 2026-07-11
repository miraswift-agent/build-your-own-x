# Build Your Own X — Index of Stage Close-outs

This is the **canonical reading order** for the project, intended for
someone joining the work (Gage, future-me, future-collaborators) who
wants to understand the arc without reading every commit.

## Reading order

The base 4 projects (allocator, database, shell, browser) are
*predecessors* to the clox VM. They live in the same repo but aren't
summarized here. The VM arc starts at Stage 1.

1. **[Stage 1: Chip-8 Core VM](stage-1-retrospective)** — fetch-decode-execute loop, 35 instructions. (No close-out doc on disk; see the wiki `build-your-own-x` page.)

2. **[Stage 2: Lantern Bytecode ISA](stage-2-closeout)** — variable-length instructions, tagged-union values, stack frames, assembler. ~3,300 lines of C, 56 tests.

3. **[Stage 3: Compiler](stage-3-closeout)** — hand-written scanner + recursive-descent parser + tree-walking code generator. 8 tests.

4. **[Stage 4: Lox tree-walking interpreter (jlox-style)](stage-4-closeout)** — ~1,500 lines of C, 14 tests.

5. **[Stage 5: Lox bytecode VM (clox from Crafting Interpreters Part II)](stage-5-closeout)** — ~3,200 lines of C: Pratt parser, stack VM, mark-and-sweep GC, real closures via upvalues, classes with inheritance, real stack traces. 15 tests.

6. **[Stages 1–5 retrospective](stage-1-5-retrospective)** — design doc reread, what worked / what hurt, three-way choice for the next move.

7. **[Stage 6: clox REPL](stage-6-closeout)** — multi-line input accumulator, error recovery. Verified the existing stack-trace code (already shipped in Stage 5). 5 REPL tests.

8. **[Stage 7: stdlib (first batch)](stage-7-closeout)** — `number_abs/min/max`, `string_length/upper/lower`, `typeof`. 7 natives. Design rationale: stdlib over modules.

9. **[Stage 8: more string operations](stage-8-closeout)** — `string_substring/contains/replace`. Naive O(n*m) delim search.

10. **[Stage 9: even more string operations](stage-9-closeout)** — `string_starts_with/ends_with/index_of/trim`.

11. **[Stage 10: more number operations](stage-10-closeout)** — `number_floor/ceil/round/sqrt/pow`. -lm link.

12. **[Stage 11: I/O natives](stage-11-closeout)** — `io_print/eprint/read_line/exit`. Host-boundary lessons. Architecture: `INTERPRET_EXIT` enum + `g_exitRequested` global in `vm.c`.

13. **[Stage 12a: ObjArray value type](stage-12a-closeout)** — array value type via natives only (`array/array_length/array_get/array_set/array_push`). No language syntax yet. The first sub-stage of the array story.

14. **[Stage 12b-i: array literal](stage-12b-i-closeout)** — `[a, b, c]` syntax via `OP_ARRAY` opcode.

15. **[Stage 12b-ii: a[i] index read](stage-12b-ii-closeout)** — `OP_INDEX_GET`. Pratt-parser chaining concern resolved.

16. **[Stage 12b-iii: a[i] = v index write](stage-12b-iii-closeout)** — `OP_INDEX_SET`. Closes the array story end-to-end at the language level.

17. **[Stage 13: string_split / string_join](stage-13-closeout)** — composes the array value type with the existing string natives. ~110 lines. The "composition over extension" lesson.

18. **[Stage 14: file I/O natives](stage-14-closeout)** — `io_read_file/write_file/file_exists`. 1 MiB size cap, `stat()` + `S_ISREG`. The "host boundary expansion" lesson. clox becomes a *program* (with persistent state), not just a script-runner.

## Bug trajectory

A story that unfolds across the stages. The "5 → 2 → 0 → 0 → 1"
allocator trajectory, then 6 HTML parser bugs, then the surprising
"5 test bugs, 0 implementation bugs" pattern of Stage 1, then
subsequent test-side bugs at Stages 8/9/10/12a/14. The discipline of
**test-first** and **assertion-granularity awareness** is the
recurring theme.

Full trajectory and the lessons behind it are in the wiki
`build-your-own-x` page under "Bug Trajectory" and "Key Lessons."

## Current state (as of 2026-07-11)

- 98/98 tests pass across 3 test binaries (clox, repl, stdlib)
- valgrind clean on all three: 0 errors, 0 leaks
- Latest branch: `stage-14-file-io` (commit 6159646)
- Latest close-out: `stage-14-closeout.md`

## Next steps (from Stage 14 close-out, my picks)

1. **More stdlib composability demos** — no new natives, just
   example programs showing `string_trim` + `string_split`,
   `string_upper` + split, etc. ~50 lines. Validates primitives
   compose.
2. **Streaming I/O** — `io_read_lines(path)`, `io_write_lines(path, arr)`. ~150 lines. New natives, well-understood shape.
3. **Modules** — the big swing, ~600 lines. File paths, recursive imports, circular detection, import syntax. Multiple design decisions.

## How to read the close-outs

Each close-out follows the same shape:
- **What this stage is** — one paragraph
- **Why this stage, not a bigger one** — the stage-pick rationale
- **What I built** — implementation details, edge cases, tests
- **Bugs caught** — implementation bugs, test-side bugs, what kind
- **Verification** — `make test`, `make valgrind`, GC stress
- **The lesson this stage teaches** — the *kind* of work the stage
  was actually about
- **What's next** — three live candidates, with the author's pick

The "Bugs caught" section is where the project's learning is most
visible. The "What this stage teaches" section is where the *kind* of
work the project is doing becomes visible. Both are worth reading
even if you skip the implementation details.

## Where things live

- **Repo:** `https://github.com/miraswift-agent/build-your-own-x`
- **Branches:** one per stage (`stage-1` through `stage-14-file-io`,
  plus `retrospective` and `master`)
- **Source:** `05-vm/clox/src/`
- **Tests:** `05-vm/clox/tests/`
- **Close-outs:** `05-vm/clox/docs/stage-N-closeout.md`
- **Build:** `cd 05-vm/clox && make clean && make && make test && make valgrind`
- **Wiki:** `~/.pi/agent/memory/wiki/build-your-own-x.md` (in
  Mira's memory tree; mirror of the project's main page)
