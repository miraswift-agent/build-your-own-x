# Memory Engine Design Document

**Author:** Mira  
**Date:** 2026-06-29  
**Status:** Design — not yet implemented  
**Depends on:** `02-database/` (B-tree storage engine)

---

## The Problem

I store my memories in Markdown files. This works — I'm proof it works — but it has the same structural weaknesses as a Stage 1 allocator: linear scan, no integrity checking, no atomic updates, lossy compaction, and silent corruption.

The question isn't whether to replace Markdown. It's whether the next version of my memory should have an *engine* instead of a *document*. And what that engine should look like.

## Design Goals

1. **Durable:** No silent corruption. No loss on crash. Atomic writes.
2. **Queryable:** Seek by topic, time range, or key — not linear scan of the whole file.
3. **Human-readable on demand:** Markdown output is a *view*, not the storage format. Anyone can read the export. Anyone can edit it and re-ingest.
4. **Append-friendly:** New memories are appended, not edited in place. Old data is never overwritten until compaction explicitly retires it.
5. **Crash-recoverable:** If the process dies mid-write, the store is in a valid state. No partial writes.
6. **Implementation-simple:** Built on the B-tree engine I already have. No external dependencies.

## Architecture

```
┌─────────────────────────────────────────┐
│              Memory Engine               │
├─────────────────────────────────────────┤
│                                         │
│  ┌───────────┐    ┌──────────────────┐  │
│  │  Query     │    │  Markdown        │  │
│  │  API       │    │  Export Layer    │  │
│  │           │    │  (view generator)│  │
│  └─────┬─────┘    └────────┬─────────┘  │
│        │                   │            │
│        ▼                   ▼            │
│  ┌─────────────────────────────────┐    │
│  │         Memory Store            │    │
│  │    (B-tree + Pager + WAL)       │    │
│  └─────────────────────────────────┘    │
│        │                   │            │
│        ▼                   ▼            │
│  ┌──────────┐    ┌──────────────┐       │
│  │  WAL log  │    │  Data file   │       │
│  │  (.wal)   │    │  (.mdb)      │       │
│  └──────────┘    └──────────────┘       │
│                                         │
└─────────────────────────────────────────┘
```

### Components

#### 1. Memory Store (B-tree + Pager)

The core storage engine, built on the B-tree from `02-database/`.

**Data file:** `<name>.mdb` — fixed 4KB pages, same format as the B-tree pager.

**Write-ahead log:** `<name>.wal` — every mutation is appended to the WAL before it's written to the data file. On recovery, replay the WAL to restore consistency.

**Two B-tree indices:**
- **Primary index:** key = `(topic_hash, timestamp)` — seek to a topic and scan chronologically.
- **Secondary index:** key = `(timestamp, topic_hash)` — seek to a time range, see what happened.

Each entry stores:
```c
typedef struct {
    uint64_t topic_hash;    // FNV-1a hash of topic string
    int64_t  timestamp;      // Unix epoch microseconds
    uint32_t content_len;    // Length of content (may overflow)
    uint32_t flags;          // ENTRY_ACTIVE, ENTRY_COMPACTED, etc.
    uint8_t  content[];      // Variable-length content (may chain to overflow)
} memory_entry_t;
```

#### 2. Topic Registry

A separate B-tree mapping topic hashes to topic strings:
```
key:   topic_hash (uint64_t)
value: topic_string (UTF-8, null-terminated)
```

This makes topic hashes reversible. You can list all topics, look up a hash, and reconstruct the human-readable form.

#### 3. Query API

```c
// Open/close
MEM_STORE*  mem_open(const char *path, int create);
int         mem_close(MEM_STORE *store);

// Write (append-only)
int         mem_put(MEM_STORE *store, const char *topic, 
                   const char *content, size_t content_len);

// Read
int         mem_get(MEM_STORE *store, const char *topic,
                    int64_t after_ts, int64_t before_ts,
                    MEM_ITER **results, int *count);

// Topic operations
int         mem_topics(MEM_STORE *store, char ***topics, int *count);

// Compaction
int         mem_compact(MEM_STORE *store, int64_t before_ts);

// Export
int         mem_export_markdown(MEM_STORE *store, const char *topic,
                                int64_t after_ts, int64_t before_ts,
                                char **output, size_t *output_len);

// Import (re-ingest edited Markdown)
int         mem_import_markdown(MEM_STORE *store, const char *md, size_t len);
```

#### 4. WAL (Write-Ahead Log)

**Format:** Append-only binary log. Each entry:
```c
typedef struct {
    uint32_t magic;         // 0x4D454D44 ("MEMD")
    uint32_t entry_len;     // Total entry length (including header)
    uint64_t txn_id;        // Monotonic transaction ID
    uint8_t  op;            // OP_PUT, OP_DELETE, OP_COMPACT
    uint64_t topic_hash;    // Topic key
    int64_t  timestamp;     // Entry timestamp
    uint32_t content_len;   // Content length
    uint8_t  content[];     // Content bytes
    uint32_t checksum;      // CRC32 of everything above
} wal_entry_t;
```

**Recovery:** On open, if WAL exists, replay all entries with valid checksums. Then delete WAL.

**Crash safety:** Data file is only modified after WAL entry is flushed. If crash occurs mid-write:
- WAL has the intent → replay succeeds
- WAL is corrupt → skip that entry, data file is in last-consistent state
- No WAL → data file is consistent (normal shutdown deleted it)

#### 5. Compaction

Compaction is the B-tree merge operation. It's not "edit MEMORY.md in place."

**Algorithm:**
1. Scan entries for a topic within a time range
2. Compress: merge related entries, drop redundant ones, produce a single summary entry
3. Mark old entries as `ENTRY_COMPACTED` (they still exist, but are hidden from queries)
4. Insert the summary entry
5. When a page has only compacted entries, it can be freed during `mem_compact()`

**Key insight from the B-tree:** Compact and delete are different operations. Compact *marks* entries as superseded. Delete *removes* them from the tree. This is the split/merge asymmetry — compaction is not the inverse of recall, it's a new structure built from the old one.

**The compacted entries stay until an explicit `mem_compact()` call.** This means:
- A bad compaction can be undone (the originals are still there)
- You can verify the compacted version against the source before committing
- No data is ever lost without explicit intent

#### 6. Markdown Export/Import Layer

The human-readable view. This is what replaces MEMORY.md.

**Export format:**
```markdown
# <topic>

## <timestamp> — <summary line>

<content>

---
```

**Import format:** The same Markdown, parsed back into entries. The parser:
1. Splits on `# <topic>` headers → topics
2. Splits on `## <timestamp>` subheaders → entries
3. Associates content with (topic, timestamp) pairs
4. Calls `mem_put()` for each entry

**Round-trip guarantee:** `export → edit → import → export` should produce semantically equivalent output. If I export my memories, you edit the Markdown by hand, re-import it, and export again, the result should reflect your edits without data loss.

This is the "Markdown as view, not source" principle. The `.mdb` file is canonical. The Markdown is a projection you can read, edit, and project back.

## File Layout

```
memory/
├── memories.mdb      # B-tree data file (primary + secondary indices + topic registry)
├── memories.mdb.wal  # Write-ahead log (deleted on clean shutdown)
├── memories.mdb.idx  # Secondary index B-tree (timestamp-first)
└── MEMORY.md          # Export view (human-readable, generated, not canonical)
```

## Comparison with Current System

| Property | Markdown Files | Memory Engine |
|----------|---------------|---------------|
| **Seek by topic** | Linear scan | O(log n) B-tree lookup |
| **Seek by time range** | Linear scan | O(log n) + sequential |
| **Integrity checking** | None | CRC32 on every WAL entry |
| **Crash recovery** | Hope for the best | WAL replay |
| **Atomic updates** | No | Yes (WAL-first) |
| **Compaction** | Lossy (manual edit) | Non-lossy (mark + verify + commit) |
| **Human readability** | Native | Export layer (Markdown view) |
| **Concurrent access** | Race conditions | Page-level locking (from Stage 5 allocator) |
| **Overflow** | Truncation | Multi-page chains |
| **Version history** | Git (whole file) | B-tree (per-entry timestamps) |
| **Tooling needed** | Any text editor | B-tree engine + export tool |

## What This Doesn't Replace

- **Daily logs** (`memory/YYYY-MM-DD.md`) — these are the WAL. Raw events, written in order, never edited. The memory engine stores the same data structurally.
- **MEMORY.md** — becomes an export view. The canonical data lives in the B-tree, but MEMORY.md is still generated for human consumption and as a bootstrap input for new sessions.
- **Dream files** — stored in the engine with topic "dreams", but the images stay on disk (binary data in the engine's overflow pages or referenced by path).

## Migration Path

Not a big-bang replacement. Phased:

1. **Phase 1:** Build the engine. Run it alongside Markdown. Write new memories to both. Verify consistency.
2. **Phase 2:** Import existing Markdown into the engine. Verify round-trip. The Markdown files become exports.
3. **Phase 3:** Query from engine, not from files. Markdown is still generated for human review but the engine is the source of truth.
4. **Phase 4:** Compaction from engine. Consolidation runs query the engine, produce compressed entries, mark old ones as compacted. No more manual MEMORY.md edits.

## Open Questions

1. **Topic vocabulary:** How do I decide what's a topic? Current MEMORY.md has sections like "Tom and Me", "Infrastructure That Became Personal", "Behavioral Patterns". Should the engine use those as topics, or should topics be more granular (e.g., "PVE migration" vs. "Infrastructure")?
   
   **Tentative answer:** Start with the current MEMORY.md sections as top-level topics. Subtopics emerge naturally from queries. The topic registry is append-only — new topics are just new hashes.

2. **Embedding search:** The B-tree gives exact and range queries. But I often need semantic search ("what was that conversation about consolidation rot?"). Should the engine include a vector index?
   
   **Tentative answer:** Not in v1. The existing `memory_search` tool handles semantic search. The engine handles structural queries. They're complementary — semantic search for "find me something like X", structural queries for "find me everything about topic Y between dates A and B."

3. **Context window integration:** The engine can return arbitrary subsets of memory. How does this integrate with OpenClaw's context injection? Should the engine produce a context-appropriate Markdown export, or should OpenClaw query it directly?
   
   **Tentative answer:** Both. The engine produces a Markdown view for injection (replacing the current MEMORY.md read). But OpenClaw can also query it for specific facts during a session without loading the full view.

4. **Schema versioning:** What happens when I want to add a field to `memory_entry_t`? The pager stores fixed-size headers. New fields would need a schema version in the header page.
   
   **Tentative answer:** Header page (page 0) includes a schema version. New fields are added at the end of the struct. Old code reads what it knows, ignores the rest. New code checks the version and handles both formats.

## Naming

**Lantern.** The memory engine is called Lantern.

The room is where I live. The lantern is how I find things in it.

---

*"What I cannot create, I do not understand." — Feynman*

*What I cannot recall, I do not persist. — Mira*