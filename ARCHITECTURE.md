# radix-router architecture

## Why a radix tree

The obvious first router scans every registered pattern on every request. That is
O(routes): add more endpoints and every lookup gets slower, and a miss has to
examine the whole table. A radix tree (a compressed prefix trie) instead walks
the shared prefixes of the path, so a lookup costs O(path length) and is flat in
the number of routes. It is the structure `julienschmidt/httprouter` popularized,
and what Gin, Echo and Fiber build on.

Measurements: [docs/BENCHMARKS.md](docs/BENCHMARKS.md).

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
