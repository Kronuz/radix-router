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

The map is `absl::flat_hash_map` from [abseil](https://github.com/abseil/abseil-cpp) 20260817.0, which the benchmark
target fetches; the library target never links it. The segment trie is hand-written in the benchmark file. Time per
lookup (build: per table), `-O3 -DNDEBUG`, Apple clang 17.0.0 (Apple M4 Pro, macOS 26; Intel Core i9-9980HK, macOS 15,
x86-64); the last column of each machine is the spread between two runs of radix-router in the same process. The
`exact paths` comparator is a hash map of whole static paths, which cannot capture parameters.

| workload | M4 Pro: radix / segment trie | A/A | i9-9980HK: radix / segment trie | A/A |
|---|---:|---:|---:|---:|
| 16 routes, static hit | 20.5 ns / 28.2 ns | 5.0% | 37.3 ns / 56.3 ns | 7.0% |
| 16 routes, param hit | 37.2 ns / 32.4 ns | 1.2% | 71.0 ns / 59.2 ns | 3.6% |
| 128 routes, static hit | 27.9 ns / 25.2 ns | 3.1% | 58.4 ns / 52.9 ns | 2.5% |
| 128 routes, param hit | 41.5 ns / 33.3 ns | 4.2% | 89.7 ns / 61.3 ns | 3.9% |
| 128 routes, miss | 12.0 ns / 22.6 ns | 3.0% | 22.5 ns / 36.8 ns | 4.0% |
| 1,024 routes, static hit | 34.9 ns / 25.0 ns | 1.5% | 89.2 ns / 51.6 ns | 3.3% |
| 1,024 routes, param hit | 57.0 ns / 35.0 ns | 4.4% | 121.5 ns / 63.3 ns | 3.1% |
| 1,024 routes, miss | 12.1 ns / 21.0 ns | 2.5% | 22.9 ns / 37.5 ns | 0.5% |
| build, 1,024 routes | 109 us / 90 us | 0.6% | 293 us / 227 us | 12.0% |

A static hit costs 5.3 to 9.2 times a lookup in a hash map of whole paths on the M4 Pro (3.8 to 3.9 ns), and 2.6
to 5.7 times on the i9; that map answers no parameter lookups. The radix tree is ahead of the segment trie on
every miss (1.5x to 1.9x) and on static hits in the 16-route table, and behind it on parameter hits at every
size and on both lookups in the 1,024-route table (1.4x to 1.9x on both machines). The benchmark does not
isolate where the large-table gap comes from; candidates are the linear scan of the first bytes of a node's
static children and the `emplace_back` into `Params` for each captured parameter. These rows are the
starting point for any change to the node layout.
