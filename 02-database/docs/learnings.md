# B-tree Storage Engine — Learnings

## What Surprised Me

### 1. B-tree vs B+tree matters for cursor iteration
The biggest conceptual mistake was assuming this was a B+tree where all data lives in leaves. In a regular B-tree, keys with their associated values exist in **both** internal nodes (as separators) and leaf nodes. This means:
- Search only needs to find the key wherever it lives
- Cursor iteration must visit **internal node keys too**, not just leaves
- The first attempt at cursor iteration only visited leaves and missed ~20% of keys

### 2. Page-based storage adds serialization complexity
Serializing a node to exactly 4096 bytes forces careful layout decisions. The variable-length keys and values mean you can't just use fixed-size slots. The approach of storing key lengths, value lengths, and overflow flags as a header, then packing the variable data sequentially, worked but required careful offset tracking during both serialize and deserialize.

### 3. The free list is a singly-linked list — no going back
The pager's free list (page 1 as head) only stores `next_free`. Once you free a page, you can't efficiently find what was on it. This means freed pages are truly recycled — the content is gone. This is fine for a B-tree (nodes are self-contained) but would be a problem for WAL-style recovery.

## Where Bugs Hid

### 1. Cursor infinite loop (the big one)
The path-stack cursor approach had a subtle bug in the advance logic. When popping up to find the next position, the parent index wasn't being updated correctly after merging/borrowing during descent. The fix was to switch to a simpler "collect-all-then-iterate" approach for correctness, with the understanding that a streaming cursor would be needed for production use.

### 2. Split vs merge asymmetry
Split pushes the median key **up** to the parent and splits the child into two. Merge pulls the separator key **down** from the parent and combines two children. The asymmetry means:
- Split: one node becomes two, parent gains a key
- Merge: two nodes become one, parent loses a key
- If the parent (root) loses all keys, the tree shrinks in height

### 3. Delete can change the root
Deleting keys can cause the root to become empty (0 keys, 1 child). In that case, the child becomes the new root and the old root page is freed. This means `tree->root_page` must be updated **and** the header must be written back. Missing this caused persistence failures.

### 4. Pager free list must be written immediately
When freeing a page, the free list head (page 1) must be updated on disk immediately. If we just updated the in-memory pager state without writing back to disk, reopening the file would lose the free pages.

## How Page-Based Storage Changes the In-Memory Algorithm

### Read-before-write everywhere
Every operation that modifies a node must: read the page from disk, deserialize into a Node struct, modify, serialize, and write back. This is much slower than in-memory B-trees but ensures durability. Each B-tree operation may touch O(log n) pages.

### Node size is fixed at PAGE_SIZE
This means the in-memory Node struct (with Cell arrays of size 128) is much larger than what fits on a page. We can't have more than about 40-50 cells per node (depending on key/value sizes). The `max_keys = 2t-1` must be small enough to fit in a page. With t=3 (max 5 keys), this is easily satisfied.

### Overflow pages for large values
Values exceeding MAX_INLINE_VAL (256 bytes) spill to overflow pages. This adds a level of indirection but keeps nodes compact. Multi-page overflow chains allow arbitrarily large values. The two-pass read (count size, then copy) could be optimized with a linked-list approach.

## Performance Characteristics Observed

### 1000 keys with t=3
- 497 nodes (333 leaves, 164 internal)
- 499 pages allocated (~2MB)
- Tree height: ~5 levels (root page 244)

### 200 keys with t=10
- 21 nodes (20 leaves, 1 internal)
- 23 pages (~92KB)
- Much flatter tree

### Memory is zero-leak
Valgrind reports 0 bytes leaked across all 23 tests. The malloc/free pattern in the cursor and search operations is clean.

### Cursor tradeoff
The current cursor collects all entries at initialization, which is O(n) memory. For a production engine, a streaming cursor using a path stack (as initially attempted) would be O(log n) memory. The correctness-first approach paid off — the streaming cursor had subtle state machine bugs that were hard to diagnose.

## Architecture Decisions

1. **Serialization format**: Binary, big-endian, with key-length/value-length headers. Simple to parse, no alignment issues.
2. **Free list**: Singly-linked, page 1 as head. Simple but no coalescing.
3. **Header page**: Page 0 with magic number, order, page count, root page, free list head.
4. **Overflow**: Chained pages with next_page pointer and data_len. Each page holds 4090 bytes of data.
5. **No WAL, no concurrency**: Single-threaded, write-through. Production would need a write-ahead log for crash recovery.