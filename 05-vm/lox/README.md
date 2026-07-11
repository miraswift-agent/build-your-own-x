# Stage 04 — Tree-Walking Lox Interpreter

A complete C99 implementation of the Lox language from
*Crafting Interpreters* (Part I / jlox). This is the tree-walking
interpreter stage; the bytecode compiler (clox / Part II) is Stage 05.

## Build

```bash
make              # build bin/lox
make test         # build and run unit tests
make valgrind     # run tests under valgrind (0 leaks / 0 errors)
make clean        # remove build artifacts
make run FILE=... # run a Lox script
```

## Language

Implemented:
- Types: `number`, `string`, `boolean`, `nil`
- Expressions: literals, arithmetic, comparison, equality, logical
  short-circuit (`and`/`or`), unary, grouping, variables, assignment,
  function/class calls, property get/set, `this`, `super.method()`
- Statements: expression, `print`, `var`, block, `if`/`else`, `while`,
  `for` (desugared), `return`
- Functions and first-class closures
- Classes with `init`, methods, fields, and inheritance
- Native function: `clock()`

Marked TODO / not implemented:
- REPL
- Multi-file imports / modules
- Static typing
- Operator overloading
- Garbage collection — Stage 4 is batch-mode only; all heap allocations
  are tracked in an arena and freed at process exit. Long-running REPL
  or loops would accumulate memory; a real GC belongs in Stage 05.

## Layout

```
lox/
  src/
    lox.{h,c}          shared types, arena allocator, error reporting
    token.{h,c}        token types
    scanner.{h,c}        tokenizer
    ast.{h,c}          AST node definitions and constructors
    parser.{h,c}       recursive-descent / Pratt parser
    environment.{h,c}  lexical scope chain (hashmap)
    value.{h,c}        runtime value and object system
    interpreter.{h,c}  tree-walking evaluator
    main.c             CLI entry point
  tests/
    test_lox.c         unit tests
  examples/
    hello.lox
    fib.lox
    class.lox
```

## Error codes

- `0` — success
- `64` — CLI usage error
- `65` — compile / parse error
- `70` — runtime error
