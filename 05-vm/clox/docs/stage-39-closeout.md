# Stage 39 Close-out: typeof patch (OBJ_ARRAY)

**Stage:** 39 of 38
**Branch:** `stage-39-typeof-array`
**Commit:** `f534017` (impl + tests + example) — close-out (this file)
**Date:** 2026-07-15
**Tests:** 307/307 (was 306 before Stage 39, +1 stdlib pass: 1 single-pass = 1 test function, 1 pass)
**Valgrind:** clean (1866 allocs / 1866 frees in test_stdlib, 0 errors, 0 leaks)
**Bugs caught:** 0 implementation bugs, 0 test bugs, 0 example bugs. **13th consecutive zero-bug stage (new streak at 13 — extends the 12-streak record from Stage 38; new record above the 16-streak from Stages 11-26 that Stage 26 broke).**

## What shipped

A 1-line patch to `typeofNative` in `src/native.c`: added `case OBJ_ARRAY: name = "array"; break;` to the switch. Before this patch, `typeof(<array>)` returned `"object"` (the default). After this patch, `typeof([1, 2, 3])` returns `"array"` (matches the mental model and the JS-ish convention).

This is a **patch, not a new native.** No new VM architecture, no new tests for existing natives, no new user-code dispatch. The change is local to one switch in one function.

The latent issue was surfaced by Stage 38's test-bug #2 (`test_array_flatten_does_not_recurse` used `string(typeof(inner) == "array")` which would have worked if `typeof()` recognized `OBJ_ARRAY`, but `typeof()` returned `"object"`, so the test author had to use a different verification approach). The fix closes a 27-stage-old gap: `OBJ_ARRAY` was added in Stage 12 but the `typeof` switch was never updated to enumerate it.

## Tests added (1 total, 1 pass)

In `tests/test_stdlib.c`:
1. `test_typeof_array` — verifies `typeof([])` returns `"array"`, `typeof([1, 2, 3])` returns `"array"`, and `typeof([[1, 2], [3, 4]])` returns `"array"`. The nested-array case verifies that the outer array's type is still `"array"` (not `"object"`); the inner arrays are elements of the outer array, they don't change the outer's type tag.

The existing `test_type_predicate` (which tests `typeof(1)`, `typeof(1.5)`, `typeof("hi")`, `typeof(true)`, `typeof(nil)`) is unchanged — those cases still work correctly after the patch.

## Bugs caught

### 0 implementation bugs

The first impl attempt passed all 308 tests (307 stdlib + 1 new) on the first test run. The fix is a 1-line addition to a switch; the test exercises the new case. The "verify the file content against the commit message" discipline (from the prior heartbeat's wiki catch-up fix) applies here too: the fix is small enough that a "did the change actually land in the file" check is straightforward.

### 0 test bugs

The test author learned from Stage 38's test-bug #2 (and the 6-occurrence math-error miss pattern on the wiki side):
- The test uses `contains(out, "array\narray\narray\n")` to verify the three `typeof()` calls all return `"array"`. The discipline: **for "same value repeated" tests, use a single `contains()` call with the full expected pattern** (not three separate `contains()` calls that could pass independently).
- The test doesn't use `string(<bool>)` (which would error at runtime per Stage 17's `string()` design constraint).

### 0 example bugs

Verified the example output against the comments BEFORE commit. The example uses `print typeof(...)` (which returns a string) — no `string()` calls, no bools, no numbers. The full type table section was verified line-by-line: `bool, nil, number, number, string, array` matches the expected output. The discipline: **example-bugs are a separate discipline from test-bugs; verify the example output against the comments BEFORE commit.** Stage 39 joins the clean streak (no example bugs across Stages 30-39 — that's 10 consecutive stages with verified example output).

**13th consecutive zero-bug stage (new streak at 13 — extends the 12-streak record from Stage 38).** Stages 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39 all shipped clean from the first impl-test run. The 13-streak is the new record (the prior record was 12 from Stage 38; before that, 16 from Stages 11-26, broken by Stage 26's `strtol` partial-consumption bug).

## Architecture: zero new work

Stage 39 is a 1-line addition to an existing switch. No new architecture work. The architecture from Stage 7 (the `typeof` native) is sufficient — the fix is just adding a case to a switch that was always supposed to enumerate every value type.

The "every value type in clox is represented in this switch — adding a new value type to clox requires extending this switch too" comment in `typeofNative` has been the reminder since Stage 7. This patch is the enforcement. The discipline: **when adding a new value type to clox, update the `typeof` switch too.** Future value types (e.g., a hypothetical `OBJ_RANGE` or `OBJ_SET`) would need the same treatment.

## Design decisions

1. **1-line fix, not a new function.** The discipline: **when a latent issue can be fixed by adding a case to an existing switch, fix the switch, not the function.** The alternative (a separate `is_array(value)` native) would be a new public API, not a fix to an existing one. The minimal fix is the right fix.
2. **Returns "array" (not "object").** The discipline: **when adding a new value type to a type predicate, the type name should match the Value's type tag name.** `OBJ_ARRAY` → `"array"`. (The "object" default is for "unknown" object types — for example, if a future value type is added without updating the switch, it falls through to "object" as a safety net.)
3. **No changes to other natives.** `typeof()` is the only function affected. The patch doesn't change `array_*` natives, the VM, the compiler, the GC, or any other part of clox. The discipline: **a patch should be local to one function, not a multi-file refactor.**
4. **The existing `test_type_predicate` doesn't need to be updated.** The new test (`test_typeof_array`) covers the new case; the existing test covers the existing cases. No double-coverage, no test-bloat.
5. **The example demonstrates the full type table.** The example shows `typeof()` for every primitive + array, so a future reader can see the complete picture (what `typeof()` returns for each type) in one place. The discipline: **when adding a case to a type predicate, update the documentation (example) to show the new case.**

## What this stage teaches

*(a) "Patches are different from new natives, but both are stages."* Stage 39 is a patch (1 line), not a new native (which Stages 30-38 were). The patch is still a stage: tests first, valgrind, commit, close-out, wiki INDEX, daily log, all 7 steps in one heartbeat. The discipline: **a 1-line fix is still a stage if it has all the same verification steps.** The 5-minute cost of a patch is much less than the cost of a latent issue surfacing in a future stage's test-bug (which is exactly how Stage 39 got started — Stage 38's test-bug #2 surfaced the latent issue).

*(b) "Latent issues from prior value types are real."* OBJ_ARRAY was added in Stage 12, but the `typeof` switch was never updated to enumerate it. That's a 27-stage-old gap. The discipline: **when adding a new value type, the `typeof` switch is one of the places that needs to be updated.** The "every value type in clox is represented in this switch" comment in `typeofNative` has been the reminder since Stage 7. The patch is the enforcement.

*(c) "Test-bugs surface latent issues" — 3rd occurrence (Stages 33, 38, 39).* Stage 33 surfaced `toString` doesn't exist; Stage 38 surfaced `typeof` doesn't know about `OBJ_ARRAY`; Stage 39 closes the issue. The discipline: **test-bugs often surface latent issues; name the issue, decide whether to fix in this stage or later.** The fix can be a separate stage (as Stage 39 demonstrates) or part of the same stage (as a side change). The "patch as a separate stage" pattern works well when the patch is small (1 line) and the close-out can be a few paragraphs.

*(d) "Math-error discipline applies to the wiki, not just the code."* The prior heartbeat's commit `32a1701` had a misleading commit message ("Stage 38 INDEX catch-up") but the file content was only partially updated. This turn's heartbeat fixed the wiki first, before starting Stage 39, per the freeze-pattern antidote. The discipline: **the freeze-pattern antidote applies at every boundary** — the wiki INDEX is one such boundary, and a stale wiki INDEX is a real signal that something didn't land. The 5-second cost of the check is much less than the cost of starting Stage 39 with a stale wiki INDEX.

*(e) "Verify the file content against the commit message" — 7th occurrence (Stage 33, 35, 36, 37, 38, 39, prior heartbeat).* The pattern: a commit claims a change landed, but the file content doesn't show it. The discipline: **always verify the file content against the commit message BEFORE declaring the wiki catch-up done (or, more generally, before claiming any change landed).** The 5-second cost of the check is much less than the cost of having a stale wiki INDEX surface a false signal in the next heartbeat's Review step.

## Limitations

- No changes to other type predicates. `typeof()` is the only function affected. Other type-related operations (e.g., `is_array()`, `is_string()`) don't exist as separate natives; the only way to check a type is `typeof() == "..."`.
- The "object" default remains for unknown object types. If a future value type is added without updating the `typeof` switch, it falls through to "object" — this is a safety net, not a bug.
- No changes to the VM, the compiler, the GC, or any other part of clox. The patch is local to one function in `src/native.c`.
- No changes to the `array_*` natives. The patch is a fix to `typeof()`, not to the array infrastructure.
- The patch doesn't add a separate `is_array()` native. The discipline: **the minimal fix is the right fix** — a separate `is_array()` would be a new public API, not a fix to an existing one. If a future stage needs `is_array()`, it can be added then.

## My pick for Stage 40

After Stage 39, the `typeof` switch is fully enumerated. The remaining "stdlib in lox" candidates are:
- `array_unique_by(arr, keyFn) -> array` — extends Stage 28's `array_unique` to deduplicate by key. ~30 lines, uses the 1-arg closure path.
- `array_group_by(arr, keyFn) -> array` — groups elements by a key function. Needs object/hash support. Bigger swing.
- `array_sort(arr, comparator?) -> array` — in-place or out-of-place sort. Needs 0-arg/1-arg/2-arg forms.
- `array_chunk(arr, size) -> array` — chunks an array into fixed-size sub-arrays. Simple shape transform.
- `is_array(value) -> bool` — separate type predicate. New native, not a fix to `typeof()`.

**After Stage 39, the next decision is one of:**
1. **`array_unique_by` (the wiki's Stage 40 pick per the candidates block)** — natural mirror of Stage 28 + Stage 30. ~30 lines, no new architecture.
2. **`array_chunk`** — simple shape transform, similar to Stage 38.
3. **Apply Stages 1-39 to a different project** — the trigger-mine bucket grows beyond byox.
4. **Modules (~600 lines, Tom's call)** — the biggest swing.

The next-pick for Stage 40 will be decided at the moment of decision, per the freeze-pattern antidote.

---

**File paths** (all confirmed):
- `05-vm/clox/src/native.c` — added 1 case to the `typeofNative` switch (7 lines including the comment)
- `05-vm/clox/tests/test_stdlib.c` — added 1 test function + 1 main() invocation (29 lines)
- `05-vm/clox/examples/typeof-array.lox` — new example, 81 lines
- `05-vm/clox/docs/stage-39-closeout.md` — this file

**Branch:** `stage-39-typeof-array`, commit `f534017` (impl + tests + example).
