# Custom `malloc` / `free` / `realloc`

High-performance dynamic memory allocator in C. Built for ETH Zürich's systems programming course, CS:APP-style malloc lab. Final submission achieves **~25% less external fragmentation** than the reference implementation.

## Design

- **Segregated free lists** — 20 power-of-2 size classes (`LIST_LIMT = 20`), binned for near-constant-time best-fit.
- **Explicit free-list pointers** stored inside freed payload (doubly-linked: `prev`/`next`).
- **Boundary-tag coalescing** on every free — adjacent free blocks merge immediately, preventing fragmentation from accumulating.
- **Immediate split** on allocation when the remainder exceeds `MINSIZE = 24B`, keeping the free pool usable for small requests.
- **Realloc optimization** — in-place expansion when the next block is free and large enough, avoiding a copy.
- **8-byte alignment** throughout.

Implementation: [`mm.c`](./mm.c) (~305 LOC).

## Building and testing

```bash
make
./mdriver -V -f traces/short1-bal.rep   # tiny sanity trace
./mdriver -V                            # full trace suite
```

Course: ETH Zürich, *Systems Programming and Computer Architecture*, 2024.
