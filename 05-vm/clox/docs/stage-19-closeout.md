# Stage 19 Close-out — `array_reverse(arr) -> arr`

**Author:** Mira
**Date:** 2026-07-11
**Branch:** `stage-19-array-reverse` (new branch from `stage-18-array-filter`)
**Commits:** (this one — implementation + tests + close-out + example)

## What this stage is

`array_reverse(arr)` reverses an array in place and returns the
same array. The user no longer needs a hand-rolled loop to
reverse a list. This closes the third of the four language gaps
from Stage 15's composability tests.

## Why this stage

From the Stage 15 close-out's Next Steps:

> the four language gaps from Stage 15: (1) print a number in a
> sentence [closed in Stage 17], (2) hand-rolled filter [needs
> user-code dispatch, defer], (3) hand-rolled reverse [Stage 19
> pick], (4) string+number concat [defer, compiler change].

`array_reverse` is a clean native: ~25 lines, no user-function
dispatch needed (unlike `array_filter`), no compiler change
needed (unlike `string+number` concat). It's the smallest
of the four gaps, and it lands cleanly.

## What I built

### Native implementation (`src/native.c`)

```c
static Value arrayReverseNative(int argCount, Value *args) {
    if (argCount != 1) {
        runtimeError("array_reverse() takes 1 argument (%d given).", argCount);
        return NIL_VAL;
    }
    if (!IS_ARRAY(args[0])) {
        runtimeError("array_reverse() argument must be an array.");
        return NIL_VAL;
    }
    ObjArray *array = AS_ARRAY(args[0]);
    int i = 0;
    int j = array->count - 1;
    while (i < j) {
        Value tmp = array->elements[i];
        array->elements[i] = array->elements[j];
        array->elements[j] = tmp;
        i++;
        j--;
    }
    return args[0];
}
```

The reversal is an in-place two-pointer swap loop. No allocation,
no GC concern — the elements are Values (8 bytes each on 64-bit),
the swap is just moving two pointers.

### Design decision: return the array (not nil)

`array_push` (the existing mutator) returns NIL_VAL, so the user
reads the side effect via `array_length`. I chose a different
convention for `array_reverse`: **return the array**.

The reasoning:

1. **The natural return value of a reverse is the reversed array.**
   Returning nil forces the user to read the side effect via
   `print a` or `array_length(a)`, which is awkward when the
   goal is to *get* a reversed array.
2. **Chaining is natural.** `print(array_reverse([1, 2, 3]))`
   works. Without the return value, the user has to bind to a
   variable first: `var b = [1, 2, 3]; array_reverse(b); print b;`.
3. **In-place + return-self matches Python's `list.reverse()`**
   (which returns None in Python, but clox doesn't have None —
   it has nil — and clox doesn't have a "void" convention in
   natives). Returning the array is the clox-idiomatic version
   of "mutate and return the receiver for chaining."

The convention differs from `array_push` because the natural
return value of each is different. `array_push` returns nil
because the new length is what you'd want, and `array_length` is
the idiomatic way to read it. `array_reverse` returns the array
because the array itself is what you'd want.

This is a small convention call, not a load-bearing decision.
Reversing it is a 1-line edit (return NIL_VAL instead of
args[0]) plus updating the test that checks `b == a`.

### Test design (11 tests)

The tests cover the small-end cases (empty, single, two),
the medium cases (3-element, 4-element, 5-element), strings,
the returns-self property, side-effect-on-original, and
the error cases.

| Test | What it checks |
|------|----------------|
| `test_array_reverse_basic` | `[1,2,3]` → element 0 is 3, element 1 is 2, element 2 is 1 |
| `test_array_reverse_returns_self` | `b = array_reverse(a); b == a` is true |
| `test_array_reverse_empty` | `[]` reversed is still `[]`, length 0 |
| `test_array_reverse_single` | `[42]` reversed is still `[42]` |
| `test_array_reverse_two` | `[1,2]` → `[2,1]`, the smallest non-trivial case |
| `test_array_reverse_even_length` | `[1,2,3,4]` → `[4,3,2,1]`, exercises the full swap loop |
| `test_array_reverse_strings` | `["a","b","c"]` → `["c","b","a"]`, works on strings |
| `test_array_reverse_then_push` | After reverse, push still works |
| `test_array_reverse_twice` | Reversing twice returns to original order |
| `test_array_reverse_wrong_arg_count` | `array_reverse()` is a runtime error |
| `test_array_reverse_wrong_type` | `array_reverse("hello")` is a runtime error |

The two-`print` tests use `print(a[0]); print(a[1]); print(a[2])`
instead of `print(a)` because clox's `print` on an array prints
`[3, 2, 1]` on one line, not each element on its own line.

## Bugs caught

**One test bug, zero implementation bugs.** My initial tests used
`print(a)` and asserted on per-line output like `3\n2\n1\n`. But
clox's `print` on an array produces `[3, 2, 1]\n` (one line,
bracket-delimited). I caught the bug on the first test run when
7 of 11 tests failed with "expected '3\n2\n1\n' in output, got
'[3, 2, 1]\n'". Fixed by using `print(a[0])`, `print(a[1])`,
etc. — 7 tests, 7 edits, all green.

This is the **10th consecutive stage with zero implementation
bugs**. The 1 test bug is the same family as Stage 18
(mis-counted), Stage 15 (predicate assumes integer division,
clox uses float), and Stage 9 (assumed a language feature).

The test-bug pattern: when I write tests, I tend to assume the
language provides a particular output format. The clox
`print` on an array uses `[a, b, c]` format; the Lox book
language does too. I had it right in concept but forgot to
check the actual format. Easy fix.

## Verification

```
$ cd 05-vm/clox && make clean && make
[no warnings under -Wall -Wextra -std=c99 -pedantic]

$ make test
==> bin/test_clox
27 passed, 0 failed   (unchanged from Stage 18)
==> bin/test_composability
8 passed, 0 failed
==> bin/test_repl
5 passed, 0 failed
==> bin/test_stdlib
115 passed, 0 failed   (+11 from Stage 18's 104)

$ make valgrind
[0 errors, 0 leaks on all four binaries]
```

Total: **155/155 tests pass** (27 clox + 5 repl + 115 stdlib
+ 8 composability), valgrind clean on all four binaries.

### Manual smoke test of new example

```
$ ./bin/clox examples/reverse-numbers.lox
Original:
1
2
3
4
5
6
7
8
9
10
Reversed:
10
9
8
7
6
5
4
3
2
1
```

### All 10 example programs pass `make examples`

```
==> examples/age-greeting.lox       — works
==> examples/class.lox              — works
==> examples/closure.lox            — works
==> examples/csv-roundtrip.lox      — works
==> examples/fib.lox                — works
==> examples/hello.lox              — works
==> examples/line-count.lox         — works
==> examples/reverse-lines.lox      — works
==> examples/reverse-numbers.lox    — NEW, works
==> examples/wordcount.lox          — works
```

## What this stage teaches

**Convention calls deserve names, not just code.** The
return-the-array vs return-nil decision is small but it's a
convention call. Documenting it in the close-out (as above)
means the next mutator-native author can refer to the
convention by name and make a consistent choice.

The two conventions now in the codebase:

1. **Push-style mutators** (return nil, read side effect via
   `array_length`): `array_push`. Use when the natural return
   value is awkward (length, count, etc. — read via a getter).
2. **Reverse-style mutators** (return the mutated object for
   chaining): `array_reverse`. Use when the natural return
   value is the object itself.

Future mutators: `array_sort` would be reverse-style (return
the sorted array). `array_pop` is read-only (returns the
popped value, doesn't mutate the array — well, it does
remove, but the return is the value). These are case-by-case
calls.

**In-place vs new-array is a real design question.** I
considered both:

- **In-place + return-self**: matches the user expectation
  "reverse this array." No allocation. The convention is
  consistent with `array_set` (which mutates in place; the
  return is nil because there's no natural return value).
- **New array + return-new**: matches the user expectation
  "give me the reverse of this array." Allocation per call.
  No surprise mutation of the input.

I chose in-place because:
1. The Stage 15 cost was "hand-rolled reverse *loop*", and
   the hand-rolled loop is also in-place (modifies the
   original). The cleanest replacement is in-place.
2. clox arrays are values, not references, but the underlying
   `ObjArray*` is shared — so in-place is a valid operation
   (the user is mutating *their* reference's referent).
3. clox's existing mutators (`array_push`, `array_set`) are
   in-place. Consistency.

A future "give me the reverse of this array, don't mutate"
could be `array_reversed(arr)` (past tense). Not load-bearing
for the Stage 19 close-out; mentioned for the record.

## A note on what I did NOT add

- **`array_reversed(arr)`** (new-array, no mutation) —
  deferred. Not a Stage 15 cost; the in-place version
  closes the cost. If anyone wants the no-mutation version,
  it's a 5-line native.
- **`array_sort(arr, cmp)`** — needs user-function dispatch
  for the comparator (or some other sort strategy). Bigger
  than Stage 19. Defer.
- **`array_filter(arr, predicate)`** — needs user-function
  dispatch. Defer until the user-code-dispatch architecture
  is in place (likely a stdlib-in-lox stage, per Stage 15
  close-out note).

## What's next

From the Stage 17 close-out, the remaining "Stage 15 costs
documented" gaps are:

1. ~~`string(n)`~~ ✅ Stage 17
2. ~~escape sequences~~ ✅ Stage 18
3. ~~hand-rolled reverse~~ ✅ Stage 19
4. **hand-rolled filter** — needs user-code dispatch
5. **string+number concat** — needs compiler change

Of 4 documented costs, 3 are now closed. The remaining 2
(filter, string+number concat) are correctly deferred to
bigger stages.

**My pick for Stage 20**: **`array_push(arr, val) -> number`**
(change the return value from nil to the new length). This is
a *convention refinement* — bringing `array_push` in line with
the "return the natural value" convention introduced in Stage
19. ~5 lines, one design decision (return length vs return
array), fully reversible.

Alternatively, if the user wants a bigger swing, **modules**
is still the next big thing. ~600 lines. Still deferred for
Tom's call.

## Closing observation

Stage 19 is the 10th consecutive stage with zero
implementation bugs. The 1 test bug was a print-format
mismatch, caught on the first test run, fixed in 7 edits.

The "Stage 15 costs documented" list is now 3 of 4 closed.
The remaining 2 (filter, string+number concat) are correctly
deferred to bigger stages. The discipline of documenting
limitations is paying off: each deferred item has a
concrete next-step design (filter needs user-code dispatch,
string+number concat needs a compiler change), and each
closed item was small enough for a single stage.

The bug-bug-implementation trajectory continues to improve:
Stage 13 had 0 impl bugs, Stage 14 had 0, Stage 15 had 0,
Stage 16 had 0, Stage 17 had 0, Stage 18 had 0, Stage 19
had 0. The discipline of "test-first, fail the test, write
the impl, watch it pass" is the mechanism: the tests fail
on the first run with a specific, actionable error, and the
implementation is small enough to write correctly in one pass.
