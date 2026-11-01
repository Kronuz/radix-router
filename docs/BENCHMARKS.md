# radix-router benchmarks

`examples/bench.cc` (`./build/radix_router_bench`) runs the same pattern language
through a linear scan and the radix tree.

The benchmark (`examples/bench.cc`) pits the two against each other on the same
pattern language, across growing tables. The radix tree stays flat while the
linear scan climbs:

```
routes  mix            linear ns/op    radix ns/op    speedup
8       static-hit             43.8           11.3       3.9x
32      static-hit            191.1           16.3      11.7x
128     static-hit            692.9           19.3      35.9x
512     static-hit           2495.3           25.1      99.4x
1024    static-hit           5048.6           24.7     204.1x
1024    miss                10587.9           24.0     440.2x
```

(Apple M-series, `-O3`. The radix line moves from 11 to 24 ns only because of
cache and tree depth; the linear line tracks the route count.)

## Against hash-based routers

`bench/compare.cc` (`radix_router_compare`) compares `radix::Router` with two routers a program builds
from a standard container: a hash map of whole static paths (which cannot capture parameters, so it is only
the floor for static hits), and a segment trie, one hash-map level per path segment, with a static child
preferred over a parameter child. The route tables are REST-shaped (`/api/vN/resK/list`, `/:id`,
`/:id/items/:item`, `/by-name/:name/details`), chosen so the segment trie needs no backtracking and agrees
with the radix tree on every value and parameter count; the program checks that before it times a row
(`--check` runs the checks only, and is a ctest when the option is on).

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release -DRADIX_ROUTER_BUILD_BENCH=ON
cmake --build build && ./build/radix_router_compare
```

The map is `absl::flat_hash_map` when CMake finds abseil and `std::unordered_map` otherwise; the library
target never links it. Time per lookup (build: per table), `-O3`, Apple clang 17, abseil 20260817.0; the
last column of each machine is the spread between two runs of radix-router in the same process.

| workload | M4 Pro: radix / segment trie | A/A | i9-9980HK: radix / segment trie | A/A |
|---|---:|---:|---:|---:|
| 16 routes, static hit | 21.8 ns / 28.3 ns | 1.8% | 39.6 ns / 57.5 ns | 1.1% |
| 16 routes, param hit | 38.5 ns / 32.4 ns | 2.0% | 72.0 ns / 57.2 ns | 0.5% |
| 128 routes, static hit | 28.0 ns / 24.9 ns | 9.2% | 56.6 ns / 49.4 ns | 2.8% |
| 128 routes, param hit | 43.0 ns / 33.4 ns | 5.5% | 90.1 ns / 60.2 ns | 0.5% |
| 128 routes, miss | 11.9 ns / 21.1 ns | 3.1% | 25.3 ns / 36.7 ns | 0.6% |
| 1,024 routes, static hit | 42.0 ns / 25.0 ns | 1.4% | 87.2 ns / 47.9 ns | 1.0% |
| 1,024 routes, param hit | 61.5 ns / 34.6 ns | 0.2% | 122.8 ns / 63.3 ns | 2.1% |
| 1,024 routes, miss | 12.2 ns / 21.4 ns | 1.8% | 25.1 ns / 36.4 ns | 0.6% |
| build, 1,024 routes | 108 us / 93.5 us | 0.8% | 308 us / 226 us | 0.2% |

A static hit costs 5.9 to 10.4 times a lookup in a hash map of whole paths on the M4 Pro (3.7 to 4.0 ns), and 2.8
to 5.4 times on the i9; that map answers no parameter lookups. The radix tree is ahead of the segment trie on
every miss (1.3x to 1.8x) and on static hits in the 16-route table, and behind it on parameter hits at every
size and on both lookups in the 1,024-route table (1.7x to 1.9x on both machines). The benchmark does not
isolate where the large-table gap comes from; candidates are the linear scan of the first bytes of a node's
static children and the `emplace_back` into `Params` for each captured parameter. These rows are the
starting point for any change to the node layout.
