# clox — Bytecode Virtual Machine for Lox

Stage 05 of the *Build Your Own Virtual Machine in C* project: a single-pass
bytecode compiler and stack-based virtual machine implementing the Lox
language from [Crafting Interpreters](https://craftinginterpreters.com/)
(Part II).

## What is here

- **Bytecode compiler** (`compiler.c`) — Pratt parser that emits instructions
  directly into chunks; no separate AST.
- **Stack VM** (`vm.c`) — call frames, operand stack, global variables, and
  real closures via upvalues.
- **Garbage collector** (`gc.c`) — mark-and-sweep with an explicit gray stack.
- **Object system** (`object.c`) — strings, functions, closures, upvalues,
  classes, instances, and bound methods.
- **Hash table** (`table.c`) — open addressing with linear probing, used for
  string interning, globals, fields, and methods.

## Build

```bash
make          # build bin/clox
make test     # run the test suite
make valgrind # run tests under valgrind (must be 0 leaks / 0 errors)
make clean    # remove build artifacts
```

## Run

```bash
bin/clox examples/hello.lox   # prints "Hello, world!"
bin/clox --repl               # interactive REPL
```

## Test coverage

The test suite mirrors Stage 4's 14 Lox tests plus a GC stress test that
allocates many short-lived strings/closures to force collection cycles.

## GC strategy

Collection is triggered on every heap-growing `reallocate()` once allocated
bytes exceed `vm.nextGC` (initially 1 KiB, then doubled after each
successful collection). The gray stack holds objects that have been marked
black-to-white and are waiting to have their outgoing references scanned.
Roots are: the value stack, every call frame's closure, open upvalues, the
globals table, the interned-strings table, and the currently-compiling
functions.

## Deviations from the book

- `OP_INVOKE` / `OP_SUPER_INVOKE` are omitted. Method calls compile to
  `OP_GET_PROPERTY` followed by `OP_CALL`, which is sufficient for the Lox
  language in this stage.
- Values use a simple tagged union instead of NaN boxing for portability.
