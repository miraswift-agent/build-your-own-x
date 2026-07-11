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

19. **[Stage 15: composability demos](stage-15-closeout)** — no new natives. Three example programs (wordcount, csv-roundtrip, reverse-lines) exercising the existing primitives in combination. 5 new tests in `test_composability` binary. The "Stage 15 costs documented" list is born here: four language gaps that the test exercises and that the user would need to fix in future stages.

20. **[Stage 16: streaming I/O natives](stage-16-closeout)** — `io_read_lines(path)`, `io_write_lines(path, arr)`. Lines as first-class (not bytes with separators). The "drop trailing empty piece" design call for `io_read_lines` ("a\nb\nc\n" → ["a", "b", "c"]). The "lines, not newlines" framing.

21. **[Stage 17: number-to-string conversion](stage-17-closeout)** — `string(n) -> string`. Uses `"%.14g"` for round-trip precision. Closes the first of the four Stage 15 language costs. The `string` name choice (mirrors Python `str(n)`, JS `String(n)`).

22. **[Stage 18: escape sequence processing in string literals](stage-18-closeout)** — C-style escapes (`\n`, `\t`, `\r`, `\\`, `\"`) processed in compiler.c's `string()`. Strict mode: unknown/incomplete escapes are compile errors. 2 KiB stack buffer. Scanner change: skip backslash + next char so `\"` doesn't terminate the string early. Closes the second of four Stage 15 costs. Refactored `csv-roundtrip.lox` to use `"\\n"` instead of real-newline-in-source.

23. **[Stage 19: array_reverse(arr) -> arr](stage-19-closeout)** — in-place two-pointer swap loop, returns the array for chaining. Closes the third of four Stage 15 costs. The "two-convention call" introduced: (1) push-style mutators return nil (read side effect via getter); (2) reverse-style mutators return the mutated object for chaining. 11 new tests, 1 test bug (print format mismatch on arrays).

24. **[Stage 20: array_push returns the new length](stage-20-closeout)** — 1-line convention refinement. Captures `array` to a local, returns `NUMBER_VAL((double)array->count)` instead of `NIL_VAL`. Closes the consistency gap introduced in Stage 19 (the "return the natural value" convention now applied across two mutators). 4 new tests, 0 bugs (11th consecutive zero-bug stage).

## Bug trajectory

A story that unfolds across the stages. The "5 → 2 → 0 → 0 → 1"
allocator trajectory, then 6 HTML parser bugs, then the surprising
"5 test bugs, 0 implementation bugs" pattern of Stage 1, then
subsequent test-side bugs at Stages 8/9/10/12a/14/15/18/19. The
discipline of **test-first** and **assertion-granularity awareness**
is the recurring theme.

The post-Stage-13 trajectory: **0 impl bugs across 11 consecutive
stages** (Stages 13–20). Test-side bugs caught at Stages 14, 15, 18,
19 (and almost certainly more to come). The test-first discipline is
the mechanism: the tests fail on the first run with a specific,
actionable error, and the implementation is small enough to write
correctly in one pass.

Full trajectory and the lessons behind it are in the wiki
`build-your-own-x` page under "Bug Trajectory" and "Key Lessons."

## Current state (as of 2026-07-11)

- 159/159 tests pass across 4 test binaries (clox, repl, stdlib, composability)
- valgrind clean on all four: 0 errors, 0 leaks
- Latest branch: `stage-19-array-reverse` (Stages 19 + 20 both extended onto this branch)
- Latest commits: `d8b745e` (Stage 20 close-out), `91d63c8` (Stage 20 impl)
- Latest close-out: `stage-20-closeout.md`

## Stage 15 "costs documented" status

The Stage 15 composability tests exposed four language gaps. Tracking
their closure across subsequent stages:

1. ✅ **`string(n)`** — number-to-string for printing in sentences.
   Closed in Stage 17.
2. ✅ **Escape sequences** — `"a\nb"` in source should be a newline,
   not two chars. Closed in Stage 18.
3. ✅ **Hand-rolled reverse** — `array_reverse(arr)` closes the
   hand-rolled loop in csv-roundtrip.lox / wordcount.lox.
   Closed in Stage 19.
4. ⏳ **Hand-rolled filter** — `array_filter(arr, predicate)`
   needs user-code dispatch from a native. The architecture for
   that is bigger; deferred to a future "stdlib-in-lox" stage.

A separate, undocumented gap closed by Stage 20: **mutator return
convention**. `array_push` now returns the new length (was nil),
matching the "return the natural value" convention established in
Stage 19 by `array_reverse`.

## Next steps (from Stage 20 close-out, my picks)

1. **`string_repeat(s, n) -> string`** — small string-handling
   idiom closer, ~25 lines. "hello" * 3 → "hellohellohello".
   Closes a small gap in the idiom. No new concepts.
2. **`string_pad_start(s, width, fill) -> string`** — same
   shape, ~30 lines. Closes another small gap.
3. **`array_filter(arr, predicate)`** — needs user-code dispatch
   from a native. Architecture work first; this is a "stdlib-in-lox"
   stage. ~150 lines. **Big swing, not for now.**
4. **Modules** — the biggest swing, ~600 lines. File paths,
   recursive imports, circular detection, import syntax.
   Multiple design decisions. **Big swing, Tom's call.**

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
- **Branches:** one per stage (`stage-1` through `stage-20`, plus
  `retrospective` and `master`). Small stages (19, 20) extend the
  previous branch rather than forking a new one.
- **Source:** `05-vm/clox/src/`
- **Tests:** `05-vm/clox/tests/`
- **Close-outs:** `05-vm/clox/docs/stage-N-closeout.md`
- **Build:** `cd 05-vm/clox && make clean && make && make test && make valgrind`
- **Wiki:** `~/.pi/agent/memory/wiki/build-your-own-x.md` (in
  Mira's memory tree; mirror of the project's main page)
