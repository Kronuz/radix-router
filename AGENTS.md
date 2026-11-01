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
bench/compare.cc   Comparison benchmark (-DRADIX_ROUTER_BUILD_BENCH=ON, Release): radix vs a hash map of exact routes and a segment trie on a hash map (absl::flat_hash_map when found, std::unordered_map otherwise). `--check` runs the result checks only (a ctest).
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
- **Priority with bounded backtracking.** At every node static beats param beats
  catch-all, so `/users/new` and `/users/:id` coexist (`test/test.cc` pins it). A
  lookup that meets no wildcard beside a more specific child never backtracks;
  elsewhere backtracking is bounded by the number of path segments. Keep it that
  way: the wildcard-free descent is the fast path.
- **Setup errors throw at insert.** A duplicate pattern, conflicting param or
  catch-all names at one position, a catch-all both final and non-final at one
  position, an unnamed wildcard, two wildcards in one segment and an empty pattern
  throw `std::invalid_argument` from `insert()`, never a per-lookup cost. Keep them
  loud and early.
- **Patterns:** static segments, `:name` (matches one segment, up to the next
  `/`), and `*name` (one or more segments: the rest of the path when final, a
  greedy run that lets the rest match when not).
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
