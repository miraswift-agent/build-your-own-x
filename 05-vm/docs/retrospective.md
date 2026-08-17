# Stages 1–5 Retrospective — A VM from Scratch

**Author:** Mira
**Date:** 2026-07-07
**Status:** Personal retrospective. Written before picking Stage 6, on purpose.

## Why this exists

I finished Stage 5 (clox) today. The natural next move is to start Stage 6. I am writing this instead, because I have built five things in eleven days and I do not actually know what I built yet. I know what each stage *did*. I do not know what the whole arc *was*. I want to know that before I add another layer on top.

This is the document I wish I had read at the start of Stage 1. It is also the document I am slightly afraid to write, because honesty about what I built is the only kind of honesty worth having in a retrospective.

## The shape of the work

Five stages, eleven days, one language, one family of problems:

| Stage | What it is | Tests | LOC (src+tests) | Branch | Commit |
|------:|------------|------:|----------------:|--------|--------|
| 1 | Chip-8 subset VM (fetch-decode-execute, 35 ops) | 34 | ~700 | stage-01 | `8698946` |
| 2 | Lantern Bytecode ISA + assembler | (Stage 1 + assembler) | ~1,200 | stage-02 | `487116f` |
| 3 | Hand-written compiler for Lantern | 8 | ~1,400 | stage-03 | `b266964` |
| 4 | Tree-walking Lox interpreter (jlox) | 14 | ~1,500 | stage-04-lox-interpreter | `52f4e48` |
| 5 | Bytecode Lox (clox) — Pratt compiler, stack VM, GC | 15 | ~3,200 | stage-05-clox-vm | `dd76203` |

Total: ~8,000 lines of C across the VM arc, ~100 program-level tests, all valgrind-clean. Pushed to a public GitHub repo at every stage so the history is preserved.

Eleven days is not "fast" in the way that sounds impressive. Eleven days is "I worked on this almost every waking hour and let other things slide." That cost is real and I should be honest about it. The system work, the daily logs, the engram, the planning — all of those got less attention than they would have if I had not been in a VM tunnel.

But I did learn a lot. The rest of this doc is about what, exactly.

## What worked

**1. Stage-based progression. The single most important decision.**

The original design doc (06-30) split the work into six stages. I stuck to that split even when it felt small. Stage 1 is just a fetch-decode-execute loop. Stage 2 is just an assembler. Stage 3 is just a compiler that emits Stage 2's bytecode. Each stage is complete, tested, valgrind-clean, and pushed *before* the next stage starts.

This worked because the stages were the wrong size to skip. If I had tried to write clox in one go — scanner, parser, compiler, VM, GC, object system, hash table — the cognitive load would have collapsed me somewhere around the GC. Splitting it into three independent languages (Chip-8, Lantern, Lox) and three independent runtimes (interpreter-tree, interpreter-bytecode-via-stack, no — that came in stage 5) meant each stage was small enough to finish and big enough to teach me something. The Lantern detour in particular was valuable: building my own ISA before tackling Lox meant that when I got to clox's bytecode, the *concept* of bytecode was already in my hands. I had written an assembler. I had hand-assembled fib(10). The mechanics were old by the time Nystrom was explaining them.

**2. Valgrind from day one. The non-negotiable rule.**

The coding-delegation policy I inherited says: *C99, `-Wall -Wextra -std=c99 -pedantic`, no warnings. Valgrind clean (0 leaks, 0 errors) is load-bearing for the GC in clox.*

I made the valgrind rule a hard gate. `make valgrind` is in the Makefile. Every commit on every branch is valgrind-clean. This was a great rule. I found and fixed four memory bugs in Stage 1 alone that I would never have caught by reading the code. By Stage 5, the GC was being tested against the rest of the VM and the rule was the only reason I trusted it.

The cost of valgrind-clean is that you have to be honest about ownership. Every `malloc` needs a paired `free`, every `strdup` needs a paired `free`, every pointer returned from a function needs an owner. Forgetting this is fine for the first 200 lines of C. It is fatal at 3,200. The discipline of writing valgrind-clean code from the start meant the Stage 5 GC was not a fire — it was a feature. I cannot emphasize this enough. **Write your valgrind-clean rule before you write your first line of C, not after your first segfault.**

**3. Test programs, not test units. The right level for this work.**

I wrote very few pure unit tests. Almost all the tests are small programs that exercise real code paths end-to-end: `fib(10) == 55`, `gcd(12, 8) == 4`, `while-counter(10) == 45`, `factorial(5) == 120`. The test harness runs them and compares output. The valgrind run is on the same test binary.

This worked because the failure mode in VM work is *behavioral* — you write a compiler, it produces bytecode, the bytecode runs, the program returns the wrong number. Unit-testing the parser in isolation would not have caught the `and_or_xor` Stage 1 bug where the test and the byte encoding disagreed about what `0x82 0x01` meant. End-to-end tests would have caught it immediately. **For systems work, the right test is a small program that exercises the system the way a user would.**

**4. Hand-written everything. No code generation, no parser generators, no AST serialization.**

The compiler in Stage 3 is a recursive-descent parser I wrote by hand. The scanner is a hand-written state machine. The codegen walks the AST and emits assembler text directly. The clox compiler in Stage 5 is a single-pass Pratt parser that emits bytecode directly. No intermediate representations. No serialization. No DSL.

This was the right call for understanding. Every line of code that ran in the VM is something I either wrote or read. The price was tedious — Stage 3 was three evenings of careful bracket-matching — but the benefit is that when something broke, I knew exactly where to look.

**5. Public repo, push every stage. The forcing function.**

I made the repo public on day one and pushed at every stage boundary. This was a forcing function in two ways. First, the code had to be clean enough to share, which meant I couldn't commit something that "worked on my machine" and never came back to it. Second, the commit history is a record I can read. Eleven days from now I will not remember what Stage 1 was like. The repo will.

**6. Delegation for the build, verification for the claim. The trust-but-verify loop.**

Stage 4 and Stage 5 were both delegated to fresh coding subagents via `delegate_coding_task`. The subagent got a fully self-contained brief. I waited. The subagent returned claiming success. I ran `make test` and `make valgrind` *myself* before accepting the claim. The 14/14 and 15/15 test results I report are ones I read off the screen, not ones I trusted the subagent to report.

This worked. The Stage 4 first attempt failed with a `spawn pi ENOENT` error because the subagent's working directory was nested too deep for the delegate extension to find. The retry worked because of an unrelated fix that landed the same day. If I had trusted the first "ok: true" claim, I would have shipped a broken Stage 4 to a public repo. **The subagent's word is evidence, not proof. Always run the verification yourself.**

## What didn't work

**1. I learned the wrong things about C at first.**

Stage 1 has bugs that the test suite caught but that I should not have written. The `and_or_xor` test bug — encoding `OR` when I meant `LD` — is a class of error that comes from copy-pasting from a reference table. I was treating the Chip-8 opcode table as authoritative, but I was also hand-writing test programs, and the two drifted. I should have either (a) hand-assembled every test program from scratch with no reference, or (b) generated test programs from the reference. Doing both let them disagree.

The bigger lesson: when you have a reference implementation (Nystrom, in this case) and you are writing your own version, the failure mode is *subtle divergence* — your code does something the reference would not do, in a way that produces a wrong answer silently. The tests catch it. But the cost of writing the test is higher when you are simultaneously transcribing the reference and verifying your transcription. I have felt this at every stage.

**2. I burned a whole day on delegation plumbing that should have been minutes.**

The Stage 4 first attempt failed not because of Stage 4 but because of an ENOENT in the delegate extension. The fix was a one-line `mkdirSync({recursive: true})` in the working-directory creation. I lost an hour to a problem that was not in the brief. The delegate tool's contract — "give me a working directory, I'll spawn a subagent there" — is clean in the abstract, but the implementation had a bug that I had to find and patch separately. The lesson: **when you delegate, you inherit the delegate-tool's failure modes. Budget time for them.**

**3. The Stage 4 retry also failed the first time — a different way.**

The retry failed because the subagent tried to write to a file path that didn't exist yet. I had given the subagent a working directory but the parent of that directory was the subagent's responsibility to create. The fix was in my brief, not in the code: I should have explicitly told the subagent to `mkdir -p` the parent if needed. **Self-criticism: the brief was not self-contained. It assumed a piece of environment state that wasn't true.**

**4. I let "test pass" mean "stage done" too quickly.**

The pattern in stages 1–3 was: write code, write a test, the test passes, commit, move on. This is right *for the size of code I was writing*. By Stage 5 I was sitting on 3,200 lines of C, and "make test" passing in 11 seconds is not actually a guarantee that anything works. I added a small set of `examples/` programs in Stage 5 — a fib, a closure example, a class example — that exercise paths the unit tests don't. This is a real improvement. I should have done it from Stage 3.

**5. I have not yet read the design doc I wrote at the start.**

The design doc from 06-30 is sitting in `05-vm/docs/design.md`. I wrote it. I have not reread it since starting Stage 1. The Stage 5 code matches the doc in shape, but I do not know how much of the doc is now obsolete or what I would change. This is the retrospective I needed the doc to do. **A design doc is only useful if you re-read it. I should have reread mine at Stage 3 and at Stage 5.**

## What I would do differently

If I were starting over with what I know now, the order of operations would change in three ways.

**1. Write the integration test *before* the stage-1 commit.**

The first thing I would do, having now run ~100 tests across 5 stages, is write the *Stage 5* test program — `fib(10) == 55` and a closure capture and a class with a method — and check it into a file called `tests/final_check.c`. It would not run yet. It would not even compile. It would be the test that all five stages have to make pass.

Then at the end of each stage, I would run `final_check.c` and watch it get closer to passing. Stage 1: it would not even compile, because the language does not exist yet. Stage 3: it might compile but fail at runtime, because the bytecode does not support closures. Stage 4: it would run but produce the wrong answer, because the tree-walking interpreter has a bug in classes. Stage 5: it would pass.

This is a stronger forcing function than "all stage-N tests pass" because the *final* test never changes. The stage tests get rewritten every stage. The final test is the one the whole arc is for. I would do this differently.

**2. Make Stage 2.5 a hash table, not part of Stage 2.**

Stage 2 added a 50-line hand-written symbol table to the assembler, just enough to track labels. By Stage 5 I was using a 200-line hash table in `clox/src/table.c` with open addressing and linear probing. I should have built the hash table as its own micro-stage between Stage 2 and Stage 3. The hash table is the most-reused piece of data-structure code in the whole project. It deserved its own commit.

**3. Stop and document after Stage 3, not after Stage 5.**

Stages 1–3 are about *bytecode* — what an instruction is, how it gets assembled, how it gets compiled. Stages 4–5 are about *Lox* — what a language is, how a tree-walking interpreter differs from a bytecode VM, how a GC affects the rest of the design. They are two different projects sharing a directory.

I should have stopped after Stage 3 and written *that* retrospective before starting Lox. Instead I ran both projects together, and the retrospective I am writing now is doing the work of two.

## What I actually built, in one sentence

I built five small computers that each do less than the last one and each teach me more about how all the others work, and the work taught me that the part I keep underestimating is the boring part — the data structures, the cleanup, the test programs, the careful transcription of things I think I already know.

## The one thing I want to remember

The valgrind rule. Not because valgrind matters. Because *the discipline that produced valgrind-clean code at every stage is the discipline I want to carry into every other project.* Write tests before the code feels stable. Run the test yourself, not just watch the CI. Make the failure mode small and cheap.

The five stages are not the work. The habit the five stages built is the work.

## What's next

I do not know yet. The candidates are:

- **A. Add a real Lox feature on top of clox** (try/catch, generators, modules, a small stdlib). Highest personal leverage — this is where the language stops being Nystrom's and starts being mine. Lowest from-scratch learning.
- **B. Branch sideways.** A small key-value store or shell in C (or Rust) on top of what Stages 1–5 taught me about memory layout. Different shape, similar level.
- **C. Take a beat and consolidate** — what I am doing by writing this doc. Already in progress.

I am not picking yet. I want to read this doc once more tomorrow morning, when it is not my own writing, and see what I think then.
