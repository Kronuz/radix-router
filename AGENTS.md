# AGENTS

Orientation for anyone (human or agent) working on this library.

## What this is

A header-only, transport-agnostic path router. `radix::Router<T>` maps
`/`-separated path patterns to a value of any type, using a radix tree (a
compressed prefix trie). The whole library is `radix_router.h`. Lookups are
O(path length), not O(routes), and zero-copy. Read `README.md` for the API, `ARCHITECTURE.md` for the design, and
`docs/BENCHMARKS.md` for the numbers; this file is the working notes.

## File map

```
radix_router.h     The whole library: radix::Router<T> + radix::Params.
test/test.cc       ctest: static / param / catch-all / edge-split / conflict cases.
examples/bench.cc  Runnable benchmark: linear scan vs radix across table sizes.
docs/radix-router.d2  README diagram; render: d2 --layout elk --sketch --theme 302 docs/radix-router.d2 docs/radix-router.svg
```

## Dependencies

None. Pure standard C++20, header-only. That is deliberate — anything that routes
paths can drop it in, HTTP or not. Do not add dependencies.

## Conventions / invariants

- **Zero-copy lookups.** Param *names* are views into the registered pattern
  (stable for the router's lifetime); *values* are views into the path passed to
  `find()`. `find()` allocates nothing but the (reusable) `Params` vector. Do not
  introduce per-lookup allocations — the benchmark guards this.
- **httprouter semantics: static XOR wildcard at a node.** A node holds either
  static children or a single wildcard child, never both. Registering
  `/users/new` and `/users/:id` at the same position is a conflict that **throws
  at insert** — a setup-time programming error, surfaced early, never a per-lookup
  cost. Keep conflicts loud and early; don't soften them into runtime ambiguity.
- **Patterns:** static segments, `:name` (matches one segment, up to the next
  `/`), and `*name` (catch-all, allowed only as the final segment).
- **Insert may be expensive; `find()` must be tight.** The tree is built once and
  read many times. Edge-splitting / allocation in `add_route`/`insert_child` is
  fine; the read path is the hot loop.

## Build / test / bench

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build
ctest --test-dir build           # correctness
./build/radix_router_bench       # linear vs radix throughput
```

## Who uses it

Kronuz/http's `http::Router` is a thin per-method binding over this plus the HTTP
404-vs-405 distinction. This library itself stays HTTP-free; keep it that way.
