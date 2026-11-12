> **Status (2026-11-12):** the recommendation was **executed**.
> Option A shipped as merge `6ffc1ff` (close-out `0574891`); Option B
> shipped as a fast-forward of `master` from `b266964` to `0574891`
> (close-out `docs/master-fast-forward-closeout.md`). §1–§6 are kept as
> written for the record — two of their numbers were wrong in ways the
> close-outs correct (the merge's cost, and §3/§4's "reversible" vs §7's
> "irreversible"). The `main` rename (§4.4) is now the only open part,
> and it is a question for Tom.

# The repository branch state — a decision memo

**Date:** 2026-11-12 · **Measured on:** `build-your-own-x` @ `6792b12`
(main worktree `/home/mira/build-your-own-x`, branch `stage-65-sockets`)
· **Owner:** M (per the byox INDEX "Real decision points")

Every number below was measured, not remembered. The commands are in
§6 so the next session can re-run them and get the same answer.

---

## 1. The state, in one paragraph

`master` is at `b266964` — **VM Stage 3** — and is **147 commits** behind
the tip (`stage-65-sockets` @ `6792b12`). It is *an ancestor* of the tip,
so it can be fast-forwarded with a normal, non-force push. The
curriculum's real chain runs `master → … → stage-58 → stage-64-modules →
stage-65-sockets`, and the browser's work lives on a second worktree
(`~/work/byox-browser`) on `browser-harden-honesty` @ `3d2ae64`.

**But the chain has a hole in it, and it is not a layout problem.**
Stages 59, 60, 61, 62 and 63 are on a **sibling branch** that forked at
Stage 58 (`d3d3c15`) and was never merged. The trunk went Stage 58 →
Stage 64 (modules) → Stage 65 (sockets) and **skipped five stages of
clox work**: `array_union_by_key`, `array_difference_by_key`,
`array_slice`, `array_partition`, `array_flat_map`.

The evidence is at the source level, not the commit-graph level:
`git grep` for those five natives over `05-vm/clox/src` and
`05-vm/clox/tests` finds **2 files on `stage-63` and 0 on the tip**. The
trunk's clox does not implement them. Their close-out documents are on
the branch too, not on the trunk.

## 2. Three corrections to the board's own entry

The byox INDEX has carried this item since 2026-11-11. Two of its
claims are wrong, and a third thing it does not mention is the actual
problem:

| the board says | measured |
|---|---|
| "HEAD on `stage-64-modules` (`5cbdde7`)" | HEAD is on **`stage-65-sockets`** (`6792b12`) |
| "`master` … **61 stages behind**" | 61 is the **stage numbering** (Stage 3 → Stage 64). The commit distance to the tip is **147** |
| browser work at `b7f8f36` | browser work is at **`3d2ae64`** |
| — | **five stages (59–63) are stranded on a sibling branch** |

The first three are the ordinary rot of a board entry that nobody
re-measured. The fourth is the finding: **the board's framing ("fast-
forward `master`, or adopt `main`, or leave the stage-branch model")
describes a problem that does not exist, and misses one that does.**

Fast-forwarding `master` to the tip is trivial and safe — and it would
produce a "current" default branch that is still missing five stages.

## 3. The options, with their costs

**Option A — merge stage-63 into the trunk first (recommended).**
One merge brings 59→63's chain back in (`stage-63` is a descendant of
each of them ✓). Cost: 14 files, +1256/−983 lines under
`05-vm/clox/src` and `05-vm/clox/tests`, with conflicts expected —
Stage 64's modules work rewrote the VM that stages 59–63 extended, and
Stage 64's tests were split across `test_clox.c` / `test_stdlib.c`.
This is *code* work, not git work: it needs the clox suite green
afterwards.

**Option B — fast-forward `master` to the tip, and leave 59–63 where
they are.** Cost: one command (`git branch -f master stage-65-sockets`)
plus one push, and it is **reversible** (`git branch -f master
b266964` + a push back). It fixes the trap below. It does not fix the
missing five stages — it just makes them harder to notice.

**Option C — adopt `main`.** `git branch -m master main` + push +
change the remote's default branch + update the README and every doc
that says "master". Cost: naming convention only; no functional gain.
Defer it — it is a one-line decision for Tom, and it should be made
*after* the history is right, not before.

**Option D — leave the stage-branch model alone.** Cost: 60 local
branches (29 with upstream tracking, 31 local-only) and 54 remote
heads. The stage branches are the curriculum's index — the wiki cites
them by name — so they should be kept, not deleted. But 54 of them are
merged and are pure noise in `git branch`.

## 4. The recommendation

1. **Merge `stage-63` into the trunk** (Option A) — the five stages
   come back, the curriculum's numbering matches its history, and the
   clox suite gets to prove the merge.
2. **Then fast-forward `master`** to the reconciled tip (Option B) — a
   normal push, no force, reversible. This removes the trap.
3. **Keep the stage branches** (Option D) — they are the index. Do not
   delete them; do not let them hide the trunk.
4. **Defer the `main` rename** (Option C) — a convention, not a
   capability.

## 5. The trap, named

`master` is the branch a careless push lands on. It is 147 commits of
stale history, and pushing it would publish a Stage-3 tree for a
repository that is at Stage 65. The Stage 65 design doc says it, and
the sockets arc has been developing in the same repo the whole time:
**the default branch is a trap, and the fix is boring** (make it
current, or stop it being the default).

The browser's second worktree is the one branch that is *legitimately*
parallel — it is a different component under active development. It
should be merged deliberately, not swept along.

## 6. How to re-measure this

```sh
cd /home/mira/build-your-own-x
git worktree list
git branch -vv | cat
git rev-list --count master..stage-65-sockets      # 147
git merge-base --is-ancestor master stage-65-sockets && echo "fast-forwardable"
git branch --merged stage-65-sockets | wc -l        # 54
git branch --no-merged stage-65-sockets --format='%(refname:short)' | cat
git grep -l -E "flat_map|union_by_key|difference_by_key" stage-63-array-flat-map -- '05-vm/clox/src' '05-vm/clox/tests' | wc -l   # 2
git grep -l -E "flat_map|union_by_key|difference_by_key" stage-65-sockets      -- '05-vm/clox/src' '05-vm/clox/tests' | wc -l   # 0
git merge-base stage-65-sockets stage-63-array-flat-map                        # d3d3c15 (Stage 58)
```

## 7. What has NOT been done, deliberately

No branch was moved, renamed, merged or deleted. No file in
`05-vm/` was touched. The irreversible half of this item — the merge
and the fast-forward — is left for the next slice, with the clox suite
as its gate. This memo is the measurement and the recommendation;
moving branches is the decision, and the decision is Tom's to ratify.
