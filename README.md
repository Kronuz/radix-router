# radix-router

A high-performance path router for C++: map `/`-separated path patterns to a
value of any type, and look paths up in time that does not grow with the number
of routes.

```cpp
#include "radix_router.h"

radix::Router<int> r;
r.insert("/documents/count", 1);
r.insert("/documents/:id", 2);          // ":id" matches one segment
r.insert("/static/*path", 3);           // "*path" matches the rest

radix::Params params;
const int* v = r.find("/documents/42", params);   // v -> 2
params.get("id");                                  // "42" (a view, no copy)
```

## Why a radix tree

The obvious first router scans every registered pattern on every request. That is
O(routes): add more endpoints and every lookup gets slower, and a miss has to
examine the whole table. A radix tree (a compressed prefix trie) instead walks
the shared prefixes of the path, so a lookup costs O(path length) and is flat in
the number of routes. It is the structure `julienschmidt/httprouter` popularized,
and what Gin, Echo and Fiber build on.

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

## Patterns

A pattern is a sequence of `/`-separated segments:

- **static**, e.g. `/documents/count`
- **`:name` param** matches exactly one segment, up to the next `/`
- **`*name` catch-all** matches the remainder of the path, and is only allowed as
  the final segment

Captured `:name` / `*name` values are reported through `Params` at lookup time.
As in httprouter, a node holds either static children or a single wildcard child,
never both: registering `/users/new` and `/users/:id` at the same position is a
conflict and throws from `insert()` (a programming error, surfaced at setup, not
a per-lookup cost).

## Zero-copy

Lookups allocate nothing on the hot path. Parameter **names** are views into the
registered pattern (stable for the router's lifetime); parameter **values** are
views into the path you pass to `find()`. Both are `std::string_view`, so reading
them copies nothing. The only allocation is `Params` growing its small vector,
which you can reuse across lookups (`find()` clears it for you).

## Transport-agnostic

This library knows nothing about HTTP: it maps path strings to values. The HTTP
binding (per-method trees, an `HttpHandler` adapter, 404 vs 405) lives one layer
up, in [Kronuz/http](https://github.com/Kronuz/http). Anything that routes paths
can use this directly.

## Build

Header-only; drop in `radix_router.h`, or pull it in with FetchContent:

```cmake
FetchContent_Declare(radix_router
	GIT_REPOSITORY https://github.com/Kronuz/radix-router.git GIT_TAG main)
FetchContent_MakeAvailable(radix_router)
target_link_libraries(your_target PRIVATE radix_router::radix_router)
```

To run the tests and benchmark from a checkout:

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build
ctest --test-dir build          # correctness
./build/radix_router_bench      # linear vs radix throughput
```
