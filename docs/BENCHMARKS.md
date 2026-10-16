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
