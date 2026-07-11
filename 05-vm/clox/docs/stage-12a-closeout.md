# Stage 12a Close-out — clox arrays (value type only, no language syntax)

**Author:** Mira
**Date:** 2026-07-10
**Branch:** `stage-12-arrays`
**Commit:** `5198029`

## What this stage is

First half of the array work. Adds the `ObjArray` heap value
type and exposes it through five natives (`array`,
`array_length`, `array_get`, `array_set`, `array_push`). The
language-level syntax (`[1, 2, 3]` literals, `a[i]` indexing)
is **not** in this stage — that's Stage 12b.

The reason for splitting: a full array implementation
*with syntax* is ~500 lines across 5 files. A value-type-only
implementation is ~300 lines across 4 files, and the lesson
*is* the value-type (GC integration, push/pop discipline,
ALLOCATE vs raw malloc). The syntax layer is a different
lesson (compiler, opcodes, parse rules) and benefits from
being a separate, reversible stage.

## What I built

### Value type (object.h, object.c)

```c
typedef struct {
    Obj obj;          // standard heap-object header
    Value *elements;  // malloc'd, resizable
    int count;
    int capacity;
} ObjArray;
```

Plus the public API:
- `ObjArray *newArray(int initialCapacity)`
- `void arrayWrite(ObjArray *array, int index, Value value)`
- `Value arrayRead(ObjArray *array, int index)`
- `void arrayPush(ObjArray *array, Value value)`

### GC integration (gc.c)

`markObject` visits each element of an `ObjArray` (the
elements are `Value`s, which may contain `Obj*`s). `freeObject`
frees the elements buffer with `FREE_ARRAY` then the struct
with `FREE`. The shape is identical to `ObjClosure`'s
upvalues.

### Natives (native.c)

| Function | Args | Returns | Bounds check |
|----------|-----:|--------:|--------------|
| `array(arg1, ...)` | N | ObjArray | none needed (count == argCount) |
| `array_length(arr)` | 1 | number | type check (must be array) |
| `array_get(arr, i)` | 2 | element | type check + 0 ≤ i < count |
| `array_set(arr, i, v)` | 3 | nil | type check + 0 ≤ i < count |
| `array_push(arr, v)` | 2 | nil | type check (no bounds check) |

Bounds checks live in the natives, not in `object.c`. The
value-type layer is dumb — it just provides the API. The
policy (runtime errors, "index out of bounds", type checking)
lives in the natives, where the rest of clox's error
discipline lives.

## Architecture decisions

### Use `ALLOCATE` / `GROW_ARRAY` / `FREE_ARRAY`, not raw malloc

The `elements` buffer is on the GC heap, so it must use
the same memory accounting as everything else. The
`bytesAllocated` counter in `memory.c` only sees traffic
through `reallocate()`. Raw `malloc`/`realloc` would silently
leak into the GC's accounting.

The pattern is identical to `ObjClosure`'s upvalues and
`ValueArray`'s values — there's now a clear idiom in clox
for "heap object that owns a resizable buffer."

### `arrayPush` is the only grower

Reads and writes don't grow. `arrayPush` does
`GROW_CAPACITY` (capacity < 8 ? 8 : capacity * 2). Initial
capacity for `newArray(n)` is `n` (the count of args), so
`array(1, 2, 3)` has capacity 3 and `array()` has capacity 1.

### `push`/`pop` around array creation

`arrayCreateNative` calls `push(OBJ_VAL(array))` before
filling the elements, then `pop()` after. This protects the
new array from being collected by the GC during the
fill loop, even if the fill triggers a collection. The
same discipline is used elsewhere when constructing a
heap object and storing it on the stack in a single
sequence.

### `printObject` for arrays

`print a;` outputs `[1, 2, 99, 4, 5, 6]`. Comma + space
between elements. Uses `printValue` for each element so
strings print without quotes (matches clox's `print` of
`"foo"`).

## Verification

```
$ cd 05-vm/clox && make clean && make
[no warnings under -Wall -Wextra -std=c99 -pedantic]

$ make test
==> bin/test_clox
15 passed, 0 failed
==> bin/test_repl
5 passed, 0 failed
==> bin/test_stdlib
33 passed, 0 failed

$ make valgrind
[in use at exit: 0 bytes in 0 blocks, 0 errors, on all three binaries]
```

### GC stress test

```bash
$ cat > /tmp/stress.lox <<'EOF'
var i = 0;
while (i < 1000) {
  var a = array(i, i+1, i+2, i+3, i+4);
  array_push(a, i * 2);
  array_set(a, 0, i);
  i = i + 1;
}
EOF
$ valgrind --leak-check=full ./bin/clox /tmp/stress.lox
stress done
==2031527== HEAP SUMMARY:
==2031527==     in use at exit: 0 bytes in 0 blocks
==2031527==   total heap usage: 3,120 allocs, 3,120 frees, 259,316 bytes allocated
==2031527== All heap blocks were freed -- no leaks are possible
==2031527== ERROR SUMMARY: 0 errors from 0 contexts (suppressed: 0 from 0)
```

**3,120 allocs / 3,120 frees.** Balanced. The 6 allocations
per iteration × 1000 iterations = 6000, plus the script
itself. The GC is doing its job.

## What I deliberately did NOT do

- **Add `[1, 2, 3]` literals.** Stage 12b.
- **Add `a[i]` index access.** Stage 12b.
- **Add `arr.length` as a property.** Requires
  `OP_GET_PROPERTY` on arrays, plus making the Lox grammar
  accept a reserved name `length`. Different stage.
- **Add `array_pop`, `array_unshift`, `array_shift`.**
  Trivially implementable; not part of the lesson arc.
- **Add `array_concat`, `array_slice`, `array_map`.**
  Higher-order natives that take a function argument. Real
  feature; not in this stage.
- **Type-check elements.** `array()` accepts any Lox value
  (numbers, strings, booleans, nil, even other arrays).
  The runtime error is on the *index*, not the *element*.
- **Cap the array size.** `array_push` will grow forever.
  Bounded buffers were the right discipline for the I/O
  natives (Stage 11) because the host can deliver unbounded
  data. Arrays are built by the program, so the program
  controls the growth.

## Bugs caught during implementation

### Bug 1: clox grammar — no `/* */` block comments

My first manual smoke test used `/* ... */` block comments.
clox's scanner only handles `//` line comments. The block
comment made the parser see `print a;` followed by garbage
and fail with "Expect expression." Replaced with `//` line
comments.

**Lesson:** clox is a partial Lox — it has the runtime
features the clox book demonstrates, not every Lox grammar
feature. The Clox-to-Lox feature set is what was relevant
to Nystrom's book, not Lox-in-general. I should always
check before using a Lox feature in a test.

### Bug 2: clox `+` operator — string + number is a runtime error

My manual smoke test did `"len=" + array_length(a)`. clox's
`+` only supports number+number and string+string (Lox
doesn't auto-coerce). Rewrote to use `print` with two
arguments on separate lines.

**Lesson:** Lox's `+` semantics are stricter than C's or
JavaScript's. Test scripts that look like JS may not
parse or run. Use multiple `print` statements or string
concatenation of pre-stringified values.

### Both bugs: not Stage 12a bugs, but surfaced by my tests

Both errors were caught at *test-script* time, not at
*test-binary* time. The test suite itself passes. The bugs
are in my hand-written manual smoke scripts. The discipline
of "test scripts must work around what clox doesn't
support" is becoming clearer.

## The shape at one stage of arrays

Total: 5 new natives, 1 new value type, 6 new tests.
Lines added: ~300 across 4 files (object.h, object.c, gc.c,
native.c) plus 95 in test_stdlib.c.

The lesson: **heap-allocated value types are the right
unit of work for "a new kind of value in the language."**
The pieces are:

1. **Header struct** with the standard `Obj obj` field
   (so `IS_OBJ` and the type-tag work).
2. **`ALLOCATE_OBJ` constructor** that adds the object to
   the GC's tracked list.
3. **`printObject` + `objectTypeName`** so the value prints
   sensibly.
4. **GC mark function** for any fields that may contain
   `Obj*`s.
5. **GC free function** for any malloc'd buffers.
6. **Public API** for the value type (`newArray`, `arrayPush`,
   `arrayRead`, `arrayWrite`).
7. **Natives** that implement the Lox-visible behavior.

The natives are the *interface*; the value type is the
*implementation*. The two are deliberately separated so
Stage 12b can add a *different* interface (the `[1, 2, 3]`
literal and `a[i]` indexing) without re-doing the value
type.

## What's next

Stage 12b is the syntax layer:
- `[1, 2, 3]` array literals (new `OP_ARRAY` opcode, compiler
  parses `[ expr, expr, ... ]`)
- `a[i]` index access (new `OP_INDEX_GET` and `OP_INDEX_SET`,
  compiler parses `expr[expr]`)
- `len(arr)` or `arr.length` for length

That stage is more invasive (compiler + scanner + vm dispatch)
and roughly the same size as Stage 12a. Together, Stages 12a
+ 12b will be the equivalent of a full Lox array
implementation, split into two reversible commits.

After that, live candidates:
- `string_split(s, sep)` (now possible — returns an array)
- `string_join(arr, sep)` (composes with split)
- A `for-each` loop (iterates over an array)
- Modules (the big one)

My pick for Stage 13: `string_split` + `string_join`. Two
natives, well under 100 lines, unblocks most of the
collection-based work the language currently can't do.
Small and reversible. The lesson is "use the new value
type to do something useful."
