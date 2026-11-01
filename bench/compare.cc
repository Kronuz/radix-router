// radix-router comparison benchmark (-DRADIX_ROUTER_BUILD_BENCH=ON).
//
// radix::Router against a hash map of exact routes (the floor for static paths, which cannot
// capture parameters) and against a segment trie whose children sit in a hash map, the usual
// router built from a standard container. The tables are chosen so the segment trie needs no
// backtracking and agrees with the radix tree on every value and parameter count; that is
// checked before a row is timed. The map is absl::flat_hash_map when CMake finds abseil
// (HAVE_ABSL) and std::unordered_map otherwise.
//
//   radix_router_compare            all rows
//   radix_router_compare param      only rows whose workload contains "param"
//   radix_router_compare --check    the result checks only, no timing (CTest radix_router_compare)
//
// Times are the best of seven samples of at least 8 ms, in ns per lookup (per build for the
// build row).
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "radix_router.h"
#ifdef HAVE_ABSL
#include "absl/container/flat_hash_map.h"
#endif

namespace {

#ifdef HAVE_ABSL
template <class V> using Map = absl::flat_hash_map<std::string, V>;
constexpr const char* kMap = "absl::flat_hash_map";
#else
struct StringHash {
	using is_transparent = void;
	std::size_t operator()(std::string_view s) const noexcept { return std::hash<std::string_view>{}(s); }
};
template <class V> using Map = std::unordered_map<std::string, V, StringHash, std::equal_to<>>;
constexpr const char* kMap = "std::unordered_map";
#endif

using clk = std::chrono::steady_clock;

bool g_check = false;
const char* g_filter = nullptr;
int g_bad = 0;

template <class T>
inline void keep(const T& v) { asm volatile("" : : "r,m"(v) : "memory"); }

template <class F>
double measure(F&& f) {
	std::size_t it = 1;
	for (;;) {
		const auto t0 = clk::now();
		for (std::size_t i = 0; i < it; ++i) { f(); }
		if (std::chrono::duration<double>(clk::now() - t0).count() > 0.008) { break; }
		it *= 2;
	}
	double best = 1e30;
	for (int k = 0; k < 7; ++k) {
		const auto t0 = clk::now();
		for (std::size_t i = 0; i < it; ++i) { f(); }
		best = std::min(best, std::chrono::duration<double>(clk::now() - t0).count() / static_cast<double>(it));
	}
	return best * 1e9;
}

template <class A, class B>
void row(const std::string& what, const std::string& comparator, A&& ours, B&& comp, double per) {
	if (g_check) { return; }
	if (g_filter && !std::strstr(what.c_str(), g_filter)) { return; }
	const double o1 = measure(ours);
	const double c = measure(comp);
	const double o2 = measure(ours);
	const double o = std::min(o1, o2);
	std::printf("%-30s %-34s %9.2f %9.2f  %6.2f  %5.1f%%\n", what.c_str(), comparator.c_str(), o / per, c / per, o / c,
		100.0 * std::abs(o1 - o2) / o);
	std::fflush(stdout);
}

void check(bool ok, const char* what, const std::string& detail) {
	if (!ok) {
		++g_bad;
		std::printf("MISMATCH %s: %s\n", what, detail.c_str());
	}
}

// The segment trie: one hash-map level per path segment. A static child wins over the
// parameter child; there is no backtracking and no catch-all.
struct SegNode {
	Map<std::unique_ptr<SegNode>> kids;
	std::unique_ptr<SegNode> param;
	int value = -1;
};

void seg_insert(SegNode& root, std::string_view pattern, int value) {
	SegNode* n = &root;
	std::size_t i = 1;
	while (i <= pattern.size()) {
		std::size_t j = pattern.find('/', i);
		if (j == std::string_view::npos) { j = pattern.size(); }
		const std::string_view seg = pattern.substr(i, j - i);
		if (!seg.empty() && seg[0] == ':') {
			if (!n->param) { n->param = std::make_unique<SegNode>(); }
			n = n->param.get();
		} else {
			auto& kid = n->kids[std::string(seg)];
			if (!kid) { kid = std::make_unique<SegNode>(); }
			n = kid.get();
		}
		i = j + 1;
	}
	n->value = value;
}

int seg_find(const SegNode& root, std::string_view path, int* params) {
	const SegNode* n = &root;
	std::size_t i = 1;
	int np = 0;
	while (i <= path.size()) {
		std::size_t j = path.find('/', i);
		if (j == std::string_view::npos) { j = path.size(); }
		const std::string_view seg = path.substr(i, j - i);
		const auto it = n->kids.find(seg);
		if (it != n->kids.end()) {
			n = it->second.get();
		} else if (n->param && !seg.empty()) {
			n = n->param.get();
			++np;
		} else {
			return -1;
		}
		i = j + 1;
	}
	*params = np;
	return n->value;
}

// A REST-shaped table: static leaves and routes with one or two parameters.
std::vector<std::string> make_patterns(int count) {
	std::vector<std::string> v;
	for (int i = 0; i < count; ++i) {
		std::string r = "/api/v" + std::to_string(1 + i % 3) + "/res" + std::to_string(i);
		switch (i % 4) {
			case 0: r += "/list"; break;
			case 1: r += "/:id"; break;
			case 2: r += "/:id/items/:item"; break;
			default: r += "/by-name/:name/details"; break;
		}
		v.push_back(r);
	}
	return v;
}

void replace_first(std::string& s, const std::string& from, const std::string& to) {
	const auto pos = s.find(from);
	if (pos != std::string::npos) { s.replace(pos, from.size(), to); }
}

void run() {
	for (const int count : {16, 128, 1024}) {
		const std::vector<std::string> patterns = make_patterns(count);
		radix::Router<int> router;
		SegNode seg;
		Map<int> exact;
		for (int i = 0; i < count; ++i) {
			router.insert(patterns[i], i);
			seg_insert(seg, patterns[i], i);
			if (i % 4 == 0) { exact[patterns[i]] = i; }
		}

		std::vector<std::string> statics, params, misses;
		std::vector<int> static_value, param_value;
		for (int i = 0; i < count; ++i) {
			if (i % 4 == 0) {
				statics.push_back(patterns[i]);
				static_value.push_back(i);
			} else {
				std::string p = patterns[i];
				replace_first(p, ":id", "12345");
				replace_first(p, ":item", "abc9");
				replace_first(p, ":name", "widget");
				params.push_back(p);
				param_value.push_back(i);
			}
			misses.push_back("/api/v1/nores" + std::to_string(i) + "/x/y");
		}

		// Every comparator gives the radix tree's answer, including the number of parameters.
		radix::Params pr;
		for (std::size_t i = 0; i < statics.size(); ++i) {
			const int* h = router.find(statics[i], pr);
			int np = 0;
			check(h && *h == static_value[i] && exact.at(statics[i]) == static_value[i] && seg_find(seg, statics[i], &np) == static_value[i],
				"static", statics[i]);
		}
		for (std::size_t i = 0; i < params.size(); ++i) {
			const int* h = router.find(params[i], pr);
			int np = 0;
			const int v = seg_find(seg, params[i], &np);
			check(h && *h == param_value[i] && v == param_value[i] && static_cast<int>(pr.size()) == np, "param", params[i]);
		}
		for (const auto& m : misses) {
			int np = 0;
			check(router.find(m) == nullptr && seg_find(seg, m, &np) == -1, "miss", m);
		}

		const std::string tag = std::to_string(count) + " routes ";
		const std::string segtrie = std::string("segment trie on ") + kMap;
		const auto radix_pass = [&](const std::vector<std::string>& v) {
			return [&] {
				radix::Params p;
				for (const auto& s : v) {
					const int* h = router.find(s, p);
					keep(h);
					keep(p);
				}
			};
		};
		const auto seg_pass = [&](const std::vector<std::string>& v) {
			return [&] {
				for (const auto& s : v) {
					int np;
					keep(seg_find(seg, s, &np));
				}
			};
		};
		row(tag + "static hit", segtrie, radix_pass(statics), seg_pass(statics), static_cast<double>(statics.size()));
		row(tag + "static hit", std::string("exact paths in ") + kMap, radix_pass(statics), [&] {
				for (const auto& s : statics) { keep(exact.find(std::string_view(s))); }
			}, static_cast<double>(statics.size()));
		row(tag + "param hit", segtrie, radix_pass(params), seg_pass(params), static_cast<double>(params.size()));
		row(tag + "miss", segtrie, radix_pass(misses), seg_pass(misses), static_cast<double>(misses.size()));
	}

	// Building a table of 1024 routes.
	std::vector<std::string> patterns;
	for (int i = 0; i < 1024; ++i) {
		patterns.push_back("/api/v" + std::to_string(1 + i % 3) + "/res" + std::to_string(i) + (i % 2 ? "/:id" : "/list"));
	}
	row("build 1024 routes", std::string("segment trie on ") + kMap, [&] {
			radix::Router<int> r;
			for (int i = 0; i < 1024; ++i) { r.insert(patterns[i], i); }
			keep(r);
		}, [&] {
			SegNode s;
			for (int i = 0; i < 1024; ++i) { seg_insert(s, patterns[i], i); }
			keep(s);
		}, 1.0);
}

}  // namespace

int main(int argc, char** argv) {
	for (int i = 1; i < argc; ++i) {
		if (std::strcmp(argv[i], "--check") == 0) { g_check = true; }
		else { g_filter = argv[i]; }
	}
	if (!g_check) {
		std::printf("# map: %s\n", kMap);
		std::printf("%-30s %-34s %9s %9s  %6s  %6s\n", "workload", "comparator", "ours ns", "comp ns", "ratio", "A/A");
	}
	run();
	if (g_bad) {
		std::fprintf(stderr, "%d lookups disagree with their comparator\n", g_bad);
		return 1;
	}
	if (g_check) { std::printf("all radix-router comparison checks passed\n"); }
	return 0;
}
