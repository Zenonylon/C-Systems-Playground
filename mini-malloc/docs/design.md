# Design Notes

## Overview

Mini Malloc implements a heap allocator using an **explicit free list** with **first-fit** search and **boundary tag coalescing**.

---

## Heap Growth: `sbrk`

New memory is requested from the OS via `sbrk(increment)`, which extends the heap break pointer by `increment` bytes and returns the old break address.

```c
void *p = sbrk(size);  // returns start of newly allocated region
```

`sbrk` is simple and sufficient for a learning allocator. Unlike `mmap`, it gives a contiguous heap, which makes pointer arithmetic and coalescing straightforward.

---

## Block Layout

Every allocation (free or in-use) is preceded by a **header** and followed by a **footer** (boundary tags). This allows O(1) coalescing with adjacent blocks.

```
+--------+-------------------+--------+
| header |   payload ...     | footer |
+--------+-------------------+--------+
   8 B       size bytes          8 B
```

### Header / Footer fields

| Field  | Bits  | Description                          |
|--------|-------|--------------------------------------|
| size   | 63–1  | Total block size (header + payload + footer) |
| alloc  | 0     | 1 = allocated, 0 = free             |

Both header and footer store the same value, packed into a `size_t`.

```c
typedef struct {
    size_t size_alloc;  // size | alloc flag
} block_header_t;
```

---

## Free List

Free blocks are linked via an **explicit doubly-linked list** embedded inside the payload area.

```
+--------+------+------+----------+--------+
| header | prev | next | (unused) | footer |
+--------+------+------+----------+--------+
```

- `prev` / `next` — pointers to adjacent free blocks in the list
- The list is **unsorted** (LIFO insertion on `free`)

---

## Allocation: `my_malloc`

1. Round `size` up to the nearest alignment boundary (8 bytes)
2. Add header + footer overhead
3. Walk the free list (**first-fit**): return the first block with `block.size >= required`
4. If a suitable block is found: **split** it if the remainder is large enough for a minimum block
5. If no block found: call `sbrk` to extend the heap, then return the new block

---

## Deallocation: `my_free`

1. Mark the block's header and footer as free (`alloc = 0`)
2. **Coalesce** with physically adjacent free blocks (4-case boundary tag coalescing)
3. Insert the resulting free block at the head of the free list

### Coalescing cases

| Prev free? | Next free? | Action                        |
|-----------|-----------|-------------------------------|
| No        | No        | Insert block as-is            |
| No        | Yes       | Merge with next block         |
| Yes       | No        | Merge with prev block         |
| Yes       | Yes       | Merge with both               |

---

## Alignment

All allocations are aligned to **8 bytes** (sufficient for `double` and most platform ABI requirements).

```c
#define ALIGN(size) (((size) + 7) & ~7)
```

---

## Planned Extensions

- `realloc` — resize in-place if next block is free, otherwise malloc + memcpy + free
- `calloc` — malloc + memset to zero
- Multiple allocation strategies — compare first-fit vs best-fit fragmentation
