# The default branch, made current — `master` fast-forwarded

**Date:** 2026-11-12 · **Repo:** `build-your-own-x` · **Move:** `master`
`b266964` → `0574891` — a fast-forward push, no force
**Owner:** M (the byox INDEX's "Real decision points"; the board's own
framing was "fast-forward `master` or adopt `main` or leave the
stage-branch model")

---

## 1. What shipped

The remote's default branch was a **Stage-3 tree**.

```
$ git ls-remote --symref origin HEAD
ref: refs/heads/master    HEAD
b266964...    HEAD        # "Stage 03: compiler for Lantern Bytecode ISA"
```

The curriculum's chain was **156 commits** ahead on `stage-65-sockets`
@ `0574891` — the reconciled tip (stages 59–63 merged in as `6ffc1ff`).
`master` is now `0574891`, and after the close-out commit it equals the
trunk's tip exactly.

The push was a normal fast-forward: `b266964..0574891  master -> master`.
No force, no history rewritten, no commit dropped.

## 2. Why this and not the other options

The memo (`docs/branch-state-decision-memo.md`) §3 listed four options.

- **Fast-forward `master`** (done): the trap is a *fresh clone* landing
  on a Stage-3 tree — and `git push` from there publishing Stage 3 for a
  Stage-65 repository. The fix is boring, which is the point.
- **Adopt `main`**: a rename of the same pointer — a convention, not a
  capability. Deferred (§4.4), and now *ready* to be asked about: the
  condition the memo set ("made after the history is right") is met.
  **Open question appended to `memory/open-questions-for-tom.md`.**
- **Keep the stage-branch model** (stage branches): they are the
  curriculum's index — the wiki cites them by name. Kept.
- **Delete the 54 merged branches**: pure noise in `git branch`, but they
  cost nothing and carry the index. Not touched.

## 3. The measurements

| | before | after |
|---|---|---|
| remote default branch | `master` @ `b266964` (Stage 3) | `master` @ `0574891` |
| commits behind the tip | **156** | **0** |
| clox suite at the pointed-at tree | 566 green (43 + 8 + 5 + 510) | same tree, same numbers |
| push | — | fast-forward, **no force** |
| trees differing (`git diff --stat stage-65-sockets master`) | — | **0 files** |

The suite was re-run at the tree `master` now points at — identical
trees, so the default branch is green *as a branch*, not merely as a
commit that exists somewhere.

**Guards run before the move** (each one could have aborted it):

1. `git merge-base --is-ancestor master stage-65-sockets` — fast-forwardable.
2. `master` is not checked out in any worktree (the browser worktree is
   on `browser-harden-honesty`).
3. The remote's `master` equalled the local one — no divergence, so the
   push could not clobber anything.

## 4. The undo path, stated exactly

The memo called this **"reversible"** in §3 and **"irreversible"** in §7.
Both were half right, and the gap is worth closing.

- **Forward:** `git branch -f master stage-65-sockets` +
  `git push origin master` — a normal fast-forward push. No force.
- **Undo:** `git branch -f master b266964` locally, then
  `git push --force origin master` — **the undo requires a force push**,
  because moving the remote's branch back is a non-fast-forward.

Nothing is lost by the undo: every commit stays reachable from
`stage-65-sockets`, `merge/stages-59-63`, and the stage branches, and
the old tip remains in the reflog.

So the honest label is: **reversible in effect, force-push in
mechanism.** The memo's §7 wording was the accurate one for the remote;
§3/§4 described the local move. The lesson: **a document that calls a
step both reversible and irreversible has not measured the remote** —
and in a force-push decision, the direction of the fix matters more than
the adjective.

## 5. What deliberately did *not* happen

- **No branch renamed.** `master` is still `master`. The `main` rename
  stays deferred — one line, but it changes the URL every doc points
  at, and that is Tom's call.
- **No branch deleted.** The 54 merged stage branches are the index.
- **The browser worktree untouched** (`browser-harden-honesty` @
  `3d2ae64`): it is the one legitimately parallel branch, and it should
  be merged deliberately, not swept along.
- **No code changed.** The tree at `master` is byte-identical to the tree
  that was tested and valgrind'd at the trunk.

## 6. Next

A design for the next unit of work, then the unit itself. Two
candidates are on the board:

- **The browser's outer-loop iteration cap** — a documented, single
  behavioural difference: the spec says that after 8 iterations the
  algorithm should act as "any other end tag", while the component
  aborts silently (as html5lib 1.1 does). Small, but it means
  *deliberately diverging from the oracle*, which is a decision, not a
  measurement.
- **The C sockets component's chunked transfer-encoding** — a real
  protocol feature with a captured-wire oracle (a chunked response
  recorded off the wire, decoded by the component, byte-compared), the
  valgrind gate, and the same discipline Stage 65.8 used for DNS
  packets.

The second is the better-sized stage: the first needs a decision about
the oracle before it needs code, and a stage should be sized to the
window.

### What would change the pick

Tom's steer: a different branch layout (the `main` rename, which is
open), a steer back to the clox ladder, a steer into the sockets
component's other deferred items (`Host` routing, `Date:`, TLS,
`SIGPIPE`), a steer to the browser instead of the sockets, or a request
to spend the time on the Vercel publish gate. Silence does not change
the pick: the branch layout is done, and the board's remaining deferred
list is the sockets component's.
