# Stage 4: Slab Allocator

## Design

### Problem
Stages 1-3 allocate every object with a header (8 bytes). For small objects,
this is 33-50% overhead (16-byte request → 24-byte block). Real allocators
solve this with **slabs**: pre-allocated arrays of same-sized objects with
bitmap allocation instead of per-object headers.

### Architecture

```
                    ┌─────────────────────┐
                    │   mira_malloc(size)  │
                    └─────────┬───────────┘
                              │
                    ┌─────────▼───────────┐
                    │  size ≤ 256?         │
                    │  (small threshold)   │
                    └───┬─────────────┬────┘
                        │ YES         │ NO
                ┌───────▼──────┐  ┌──▼─────────────┐
                │ Slab allocator│  │ Segregated free │
                │ (bitmap-based)│  │ lists (Stage 3) │
                └──────────────┘  └─────────────────┘
```

### Slab Structure

A **slab** is a contiguous block of memory divided into fixed-size slots:

```
┌──────────┬──────────┬──────────┬───────────┬─────┐
│ Slab hdr │ slot 0   │ slot 1   │ slot 2    │ ... │
│ 32 bytes │ 16 bytes │ 16 bytes │ 16 bytes  │     │
└──────────┴──────────┴──────────┴───────────┴─────┘
│          │                    │
│          └── no per-slot header, just raw payload ──┘
```

**Slab header** (32 bytes):
- `magic`     (4 bytes): 0x5142534C ("QBSL") — sanity check
- `slot_size` (8 bytes): size of each slot
- `n_slots`   (8 bytes): number of slots in this slab
- `n_used`    (8 bytes): number of currently allocated slots
- `bitmap`    (variable): 1 bit per slot, 1=allocated, 0=free

**Key insight**: No per-object header. The bitmap tells us which slots are
free, and the slab header tells us the slot size. Given a pointer, we find
the slab by aligning down to the slab boundary, then check the bitmap.

### Size Classes (Small Objects)

| Class | Slot size | Slab capacity (4KB slab) |
|-------|-----------|--------------------------|
| 0     | 16        | 252 slots                |
| 1     | 32        | 124 slots                |
| 2     | 48        | 82 slots                 |
| 3     | 64        | 62 slots                 |
| 4     | 80        | 48 slots                 |
| 5     | 96        | 40 slots                 |
| 6     | 112       | 36 slots                 |
| 7     | 128       | 30 slots                 |
| 8     | 160       | 24 slots                 |
| 9     | 192       | 20 slots                 |
| 10    | 224       | 18 slots                 |
| 11    | 256       | 14 slots                 |

### Slab Layout

Each slab is allocated via mmap (4KB minimum, grows for larger slot sizes).
The bitmap follows the slab header. Slots start after the bitmap.

Alignment: slab header is at the start of the mmap'd region. Slots are
aligned to 8 bytes. The entire slab is aligned to SLAB_ALIGN (4096 bytes).

Given a pointer `p`, finding the slab:
1. Align `p` down to SLAB_ALIGN → potential slab start
2. Check `magic` field → if 0x5142534C, this is our slab
3. If not, the pointer came from the segregated free list allocator

### Free Operation

`mira_free(p)`:
1. Align `p` down to SLAB_ALIGN
2. If `magic == 0x5142534C`:
   - Compute slot index: `(p - slab_data_start) / slot_size`
   - Clear bit in bitmap
   - Decrement `n_used`
   - If `n_used == 0` and other slabs in same class have free slots: unmap slab
3. Else: use segregated free list (Stage 3)

### Reap Threshold

When a slab becomes empty (all slots freed), we don't immediately unmap it.
We keep one empty slab per size class as a reserve. A second empty slab gets
unmapped — this prevents thrashing for workloads that allocate/free in cycles.

### API (unchanged from Stage 3)

```c
void  *mira_malloc(size_t size);
void   mira_free(void *ptr);
void  *mira_calloc(size_t nmemb, size_t size);
void  *mira_realloc(void *ptr, size_t size);
```

### Diagnostics (extended)

```c
void   mira_heap_check(void);     // Walk both slab and free-list heaps
void   mira_print_heap(void);     // Dump slab + free-list state
size_t mira_free_space(void);     // Total free space (both allocators)
size_t mira_largest_free(void);   // Largest single free block
size_t mira_free_list_length(void); // Count in segregated lists
void   mira_print_slabs(void);    // Per-slab usage statistics
```

### Performance

| Operation | Small (slab) | Large (free list) |
|-----------|-------------|-------------------|
| malloc    | O(1) bitmap scan | O(1) bucket scan |
| free      | O(1) bitmap clear | O(1) list insert + coalesce |
| realloc   | O(n) copy if grows | O(1) if in-place |

No per-object header overhead for small allocations. Bitmap scanning is
cache-friendly and fast for partially-used slabs.