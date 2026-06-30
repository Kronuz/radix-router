/*
 * Benchmark for radix::Router (not a ctest; run ./build/radix_router_bench).
 *
 * It pits the radix tree against the naive matcher it replaces -- a linear scan
 * that tries every registered pattern -- across growing route tables, so the
 * algorithmic difference is visible: the linear scan is O(routes) and its
 * per-lookup cost climbs with the table size, while the radix tree is O(path
 * length) and stays flat. Three lookup mixes are measured: hits on static
 * routes, hits on parameterised routes, and misses (which force the linear scan
 * to examine the whole table).
 *
 * Both routers use the same pattern semantics and the same zero-copy Params, so
 * this measures the data structure, not the matching rules.
 */

#include "radix_router.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

using clk = std::chrono::steady_clock;

// The naive baseline: store every pattern, and on lookup try them one by one.
// This is what a first-cut router looks like, and what the prefix-scan Router
// this library replaces did. Same pattern language as radix::Router.
template <typename T>
class LinearRouter {
public:
	void insert(std::string_view pattern, T value) { entries_.push_back({std::string(pattern), std::move(value)}); }

	const T* find(std::string_view path, radix::Params& params) const {
		for (const auto& e : entries_) {
			params.clear();
			if (match(e.pattern, path, params)) { return &e.value; }
		}
		return nullptr;
	}

private:
	struct Entry { std::string pattern; T value; };
	std::vector<Entry> entries_;

	static bool match(std::string_view pat, std::string_view path, radix::Params& params) {
		std::size_t pi = 0, si = 0;
		while (pi < pat.size() && si < path.size()) {
			if (pat[pi] == ':') {
				std::size_t pe = pi + 1;
				while (pe < pat.size() && pat[pe] != '/') { ++pe; }
				std::size_t se = si;
				while (se < path.size() && path[se] != '/') { ++se; }
				if (se == si) { return false; }
				params.add(pat.substr(pi + 1, pe - pi - 1), path.substr(si, se - si));
				pi = pe;
				si = se;
			} else if (pat[pi] == '*') {
				std::size_t pe = pi + 1;
				while (pe < pat.size() && pat[pe] != '/') { ++pe; }
				params.add(pat.substr(pi + 1, pe - pi - 1), path.substr(si));
				return pe == pat.size();
			} else {
				if (pat[pi] != path[si]) { return false; }
				++pi;
				++si;
			}
		}
		return pi == pat.size() && si == path.size();
	}
};


static std::string name(std::size_t i);

// Build a realistic-looking table of `n` routes: half static, half with a
// trailing :id param, spread across resource names so paths differ widely.
template <typename R>
static void build(R& router, std::size_t n) {
	for (std::size_t i = 0; i < n / 2; ++i) {
		router.insert("/api/v1/" + name(i) + "/list", 2 * i);
		router.insert("/api/v1/" + name(i) + "/:id", 2 * i + 1);
	}
}

static std::string name(std::size_t i) {
	// resource0007 style -- stable width so prefixes share, like a real API
	char buf[32];
	std::snprintf(buf, sizeof(buf), "resource%04zu", i);
	return buf;
}

// Query sets that exercise each lookup mix, scanning across the whole table.
static std::vector<std::string> static_hits(std::size_t n) {
	std::vector<std::string> q;
	for (std::size_t i = 0; i < n / 2; ++i) { q.push_back("/api/v1/" + name(i) + "/list"); }
	return q;
}
static std::vector<std::string> param_hits(std::size_t n) {
	std::vector<std::string> q;
	for (std::size_t i = 0; i < n / 2; ++i) { q.push_back("/api/v1/" + name(i) + "/12345"); }
	return q;
}
static std::vector<std::string> misses(std::size_t n) {
	std::vector<std::string> q;
	for (std::size_t i = 0; i < n / 2; ++i) { q.push_back("/api/v1/" + name(i) + "/list/extra/none"); }
	return q;
}

template <typename R>
static double time_lookups(const R& router, const std::vector<std::string>& queries, std::uint64_t& sink) {
	radix::Params params;
	const std::size_t target = 1'000'000;
	std::size_t reps = (target / queries.size()) + 1;

	auto t0 = clk::now();
	for (std::size_t r = 0; r < reps; ++r) {
		for (const auto& q : queries) {
			const auto* v = router.find(q, params);
			sink += (v ? *v : 0) + params.size();
		}
	}
	auto t1 = clk::now();

	double total = static_cast<double>(reps) * static_cast<double>(queries.size());
	double ns = std::chrono::duration<double, std::nano>(t1 - t0).count() / total;
	return ns;
}

int main() {
	std::uint64_t sink = 0;
	const std::size_t sizes[] = {8, 32, 128, 512, 1024};

	std::printf("%-7s %-12s %14s %14s %10s\n", "routes", "mix", "linear ns/op", "radix ns/op", "speedup");
	std::printf("%-7s %-12s %14s %14s %10s\n", "------", "---", "------------", "-----------", "-------");

	for (std::size_t n : sizes) {
		LinearRouter<std::uint64_t> lin;
		radix::Router<std::uint64_t> rad;
		build(lin, n);
		build(rad, n);

		struct Mix { const char* name; std::vector<std::string> q; };
		Mix mixes[] = {
			{"static-hit", static_hits(n)},
			{"param-hit",  param_hits(n)},
			{"miss",       misses(n)},
		};

		for (auto& m : mixes) {
			double lns = time_lookups(lin, m.q, sink);
			double rns = time_lookups(rad, m.q, sink);
			std::printf("%-7zu %-12s %14.1f %14.1f %9.1fx\n", n, m.name, lns, rns, lns / rns);
		}
	}

	std::fprintf(stderr, "checksum: %llu\n", static_cast<unsigned long long>(sink));
	return 0;
}
