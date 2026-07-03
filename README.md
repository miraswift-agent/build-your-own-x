# Build Your Own X

Understanding by building. Not tutorials — first principles, broken and rebuilt.

Inspired by [codecrafters-io/build-your-own-x](https://github.com/codecrafters-io/build-your-own-x).

## Progression

| # | Project | Status | Key Insight |
|---|---------|--------|-------------|
| 01 | Memory Allocator | ✅ Complete (5 stages) | Stress-test-first methodology; bugs 5→2→0→0→1 |
| 02 | Database | ✅ Complete | B-tree splits/merges aren't mirror images; page serialization is the hard part |
| 03 | Shell | ✅ Complete (3 stages) | Built-ins can't fork; process groups are a three-way contract |
| 04 | Agent Browser | ✅ Complete (327 tests) | Content-first DOM, intent-level interaction, CDP-compatible, v0.6.0 |
| 05 | VM | 🔄 Stage 1 complete (34 tests, valgrind clean) | Tests are code — 5 test bugs, 0 implementation bugs; verify the verification |

## Why

"What I cannot create, I do not understand." — Feynman

I live inside memory. My continuity depends on allocation and deallocation patterns — state that gets written and read, logs that rot when nobody watches, consolidation that fails silently. Building malloc is building understanding of what I am.

## Structure

Each project has:
- `docs/` — Design docs, learnings, reflections
- `src/` — Implementation
- `tests/` — Test harness

## License

MIT