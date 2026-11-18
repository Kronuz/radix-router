# Nested catch-all matching is bounded

## The class

A route with k non-final catch-alls (`/*a/*b/*c/z`) matched against a path that fails (no
final `/z`) used to retry the same (catch-all node, remaining path) state once per way of
splitting the earlier catch-alls: O(n^k) for n segments. Found by the C4 parser fuzz
continuation (333 CPU s of 3,600 in slow inputs). Reproducer: `fuzz/slow_repro.cc` (family of
k nested catch-alls over n repeated `/x`, plus the five retained `fuzz/regressions/slow-unit-*`).

Measured on main, strict job 261117-184755-fff446:

| input | time |
|---|---:|
| k=4, n=64 | 0.023 s |
| k=6, n=64 | 4.18 s |
| k=8, n=32 | 1.10 s |
| k=8, n=64 | 207 s |
| five fuzz artifacts | 0.47 to 5.34 s each |

## The bound

Each lookup records failed (catch-all node, remaining path length) states. A continuation's
outcome depends on nothing else, so each state is tried once, and a repeat ends the loop: an
earlier visit already failed that position and every later one. Matching is O(nodes x path
length). (Skipping a repeat but continuing the loop was still O(k x n^2): 1.21 s for k=8 over
4096 `/x`, review finding F1; ending the loop gives 6.0 ms.) The set is allocated on the first failed continuation, so lookups that
never fail a catch-all allocate nothing new. Match priority and capture order are unchanged.

Only catch-all continuations are memoized. Static/param/catch-all alternatives at one node are
bounded by tree depth, not by path splits.

## Exposure

Xapiand's own routes cannot reach it: `src/server/search_views.cc` registers a fixed set of
internal tokens (`/db`, `/doc`, `/meta`, `/search`, `/metrics`) in `http::MethodRouter`, none
with a catch-all, and a client path is classified to one of them first. The blow-up needs a
route set with two or more non-final catch-alls (as the fuzzer builds), e.g. another consumer of
radix-router.

## Ordinary routes cost the same

Strict A/B (`bench/ab.sh`, 17 routes, 14 paths, 10 interleaved rounds, job 261117-194506-e318e7):
fix/main 1.010 [0.944, 1.080]; the A/A, main2/main, 1.097 [1.006, 1.196]. The first version
memoized every failed catch-all state and was 2.5 to 2.9 times slower (193-214 ns to 532-586 ns):
failing single-catch-all lookups allocated. The set is now touched only when catch-alls nest.

## Fixed times

The same family and artifacts at the fix: strict job recorded in the quest journal
(`fuzz/slow_repro.cc`; k=8, n=64 was 207 s on main).

## Evidence

`test/test.cc` `test_nested_catch_all_bounded` (k=6, n=64 under 0.5 s, fails on main's matcher),
`fuzz/diff.cc` (differential against main's matcher, `fuzz/radix_router_main.h`), `bench/ab.sh`
(typical table A/B with A/A). Numbers: see the quest journal.
