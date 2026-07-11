# Mini Malloc

A minimal memory allocator implemented in C, built to understand how `malloc`, `free`, and `realloc` work under the hood.

## Goal

Implement a working heap allocator from scratch using low-level system calls (`sbrk`/`mmap`), covering the core mechanics of free list management, block splitting, and coalescing.

## Planned Features

- [ ] `my_malloc()`
- [ ] `my_free()`
- [ ] Block reuse
- [ ] Block splitting
- [ ] Block coalescing
- [ ] `realloc()`
- [ ] `calloc()`
- [ ] Multiple allocation strategies

## Build

```bash
make        # build the library
make test   # run tests
make clean  # remove build artifacts
```

## Project Structure

```
mini-malloc/
├── src/
│   └── malloc.c       # allocator implementation
├── include/
│   └── malloc.h       # public API
├── tests/
│   └── test_malloc.c  # test cases
├── docs/
│   └── design.md      # algorithm and data structure details
└── Makefile
```

## Design

See [docs/design.md](docs/design.md) for algorithm choice, block header layout, and implementation notes.
