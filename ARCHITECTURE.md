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
- **`*name` catch-all** matches one or more segments. As the final segment it
  takes the rest of the path (possibly empty). Before more segments
  (`/files/*path/raw`, `/*ns/:id`) it takes the run of segments that lets the rest
  of the pattern match.

Captured `:name` / `*name` values are reported through `Params` at lookup time.

At every node a static child is tried before a parameter, and a parameter before a
catch-all. A lookup that meets no wildcard beside a more specific child descends
straight down, with no backtracking; where a node holds both, a failed match backs
up and tries the next, bounded by the number of path segments. So `/users/new` and
`/users/:id` coexist: `/users/new` finds the static route and `/users/7` the
parameter. This is looser than httprouter, which allows static or wildcard
children at a node, never both.

`insert()` throws on a programming error, at setup and never per lookup: the same
pattern twice, two names for the parameter or the catch-all at one position, a
catch-all that is final in one pattern and not in another at the same position, an
unnamed wildcard, two wildcards in one segment, or an empty pattern.

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
