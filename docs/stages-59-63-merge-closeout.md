# Stages 59–63 merged into the trunk — close-out

**Date:** 2026-11-12 · **Repo:** `build-your-own-x` · **Trunk:** `stage-65-sockets` @ `6ffc1ff`
**Merge branch kept as a rollback point:** `merge/stages-59-63` @ `6ffc1ff`

---

## 1. What shipped

The five stranded stages are back in the trunk:

| stage | native | branch it came from |
|---|---|---|
| 59 | `array_union_by_key` | `stage-59-array-union-by-key` (`4df19f3`) |
| 60 | `array_difference_by_key` | `stage-60-array-difference-by-key` (`f0e8758`) |
| 61 | `array_slice` | `stage-61-array-slice` (`08f558f`) |
| 62 | `array_partition` | `stage-62-array-partition` (`72e458f`) |
| 63 | `array_flat_map` | `stage-63-array-flat-map` (`1379c13`) |

Merge commit `6ffc1ff` — parents `39213c0` (the trunk) and `1379c13`
(stage-63, which descends from 59→63). 12 files, **+1902 / −119**: two
source/test files (`src/native.c`, `tests/test_stdlib.c`), five
close-out documents, five example programs.

`master` was deliberately **not** moved — see §4.

## 2. The measurements

| | before (trunk) | after (merged) |
|---|---|---|
| clox suite, total | 524 | **566** |
| `test_stdlib` | 468 | **510** |
| `test_clox` / `test_composability` / `test_repl` | 43 / 8 / 5 | 43 / 8 / 5 (unchanged) |
| conflicts | — | **0** |
| valgrind | — | **0 errors, 0 leaks**, 4 binaries, exit 0 |

The "before" column was measured in a throwaway worktree at
`stage-65-sockets` and the worktree removed — not remembered.

**+42 tests.** The five stages' tests came in, and every test the trunk
had is still here.

## 3. The findings

**The memo's conflict prediction was wrong, and it cost one command to
find out.** The branch-state memo said "conflicts expected — Stage 64's
modules work rewrote the VM that stages 59–63 extended." The merge was
**clean**: the stages' work landed in `native.c` (the natives) and
`test_stdlib.c` (their tests), in regions Stage 64's modules rewrite
did not touch. *A prediction about a merge is cheap to test; test it
before it becomes a reason to defer.* The deferral was honest but
unnecessary, and the measurement made it unnecessary.

**A clean merge is not a correct merge.** Git merged the text; the suite
proved the semantics. 524 → 566 is the evidence that five stages of work
arrived intact and that nothing the trunk had was lost on the way. The
suite is the gate — git's "no conflicts" is not.

**Two numbers, two questions.** The memo's "+1256 / −983 under
`src` and `tests`" and this merge's "+1902 / −119 over 12 files" are
both true and measure different things: the first is the *difference
between the two tips* (so it also contains Stage 64's modules rewrite
and Stage 65's sockets work — hence the deletions); the second is the
*branch's contribution*. Numbers without their question are the rot the
memo was written to fix; the memo's numbers were right, but only if you
know which question they answer.

**The hole was a history problem, and the fix was a merge, not a
rename.** "Adopt `main`" would have renamed a branch that was still
missing five stages. The curriculum's chain now reads 58 → 59 → … → 63
→ 64 (modules) → 65 (sockets), and the trunk's clox implements the five
natives the source-level search said were missing.

## 4. What deliberately did *not* happen

- **`master` was not moved.** The memo's §3 calls the fast-forward
  reversible and says it "fixes the trap"; §7 says moving branches is
  the decision and the decision is Tom's to ratify. The merge was the
  pick; the fast-forward is the next step, named below.
- **No stage branch was deleted.** The stage branches are the
  curriculum's index — the wiki cites them by name.
- **No file outside `05-vm/clox/` was touched** by the merge.

## 5. Next

**The fast-forward of `master` to the reconciled tip.** One command
(`git branch -f master stage-65-sockets`) plus a normal push — no
force, and reversible (`git branch -f master b266964` and push back).
It removes the trap the memo named: `master` is where a careless push
lands, and it is currently a Stage-3 tree for a Stage-65 repository.

### What would change the pick

Tom can redirect this in one message: a steer to the `main` rename (the
memo's Option C — one line, deferred on purpose until the history is
right), a steer to clearing the 54 merged branches (Option D's noise), a
steer back to the clox ladder, a steer to the C sockets component's
deferred items (chunked transfer-encoding, `Host` routing, `Date:`,
TLS, `SIGPIPE`), or a request to spend the time on the Vercel publish
gate instead. Silence does not change the pick: the memo's own
recommendation names the fast-forward as step 2, and it becomes honest
the moment the merge is green and pushed.
