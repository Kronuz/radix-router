// Differential: the current matcher against main's (fuzz/radix_router_main.h) on random small
// route sets and paths, and on the retained fuzz inputs. Exits nonzero on any difference.
#include "radix_router.h"
#include "radix_router_main.h"
#include <cstdio>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <vector>

static std::size_t checked = 0;

static bool same(const std::vector<std::string>& pats, const std::vector<std::string>& paths) {
	radix::Router<std::size_t> a;
	radix_main::Router<std::size_t> b;
	for (std::size_t i = 0; i < pats.size(); ++i) {
		bool ea = false, eb = false;
		try { a.insert(pats[i], i); } catch (const std::invalid_argument&) { ea = true; }
		try { b.insert(pats[i], i); } catch (const std::invalid_argument&) { eb = true; }
		if (ea != eb) { return false; }
	}
	for (const auto& path : paths) {
		radix::Params pa;
		radix_main::Params pb;
		const auto* ra = a.find(path, pa);
		const auto* rb = b.find(path, pb);
		++checked;
		if ((ra == nullptr) != (rb == nullptr) || (ra && *ra != *rb) || pa.size() != pb.size()) { return false; }
		for (std::size_t i = 0; i < pa.size(); ++i) {
			if (pa[i].first != pb[i].first || pa[i].second != pb[i].second) { return false; }
		}
	}
	return true;
}

int main(int argc, char** argv) {
	std::mt19937 rng(20261117);
	auto pick = [&](const std::string& s) { return s[rng() % s.size()]; };
	for (int it = 0; it < 200000; ++it) {
		std::vector<std::string> pats, paths;
		for (int i = 0, n = 1 + rng() % 5; i < n; ++i) {
			std::string p;
			for (int j = 0, m = 1 + rng() % 5; j < m; ++j) {
				p += '/';
				switch (rng() % 5) {
					case 0: p += "*" + std::string(1, pick("pq")); break;
					case 1: p += ":" + std::string(1, pick("xy")); break;
					default: p += std::string(1 + rng() % 2, pick("ab"));
				}
			}
			if (rng() % 4 == 0) { p += '/'; }
			pats.push_back(p);
		}
		for (int i = 0; i < 8; ++i) {
			std::string p;
			for (int j = 0, m = rng() % 7; j < m; ++j) { p += '/'; p += std::string(1 + rng() % 2, pick("ab")); }
			if (rng() % 4 == 0) { p += '/'; }
			paths.push_back(p);
		}
		if (!same(pats, paths)) { std::printf("DIFF at iteration %d\n", it); return 1; }
	}
	for (int i = 1; i < argc; ++i) {
		std::ifstream f(argv[i], std::ios::binary);
		std::string in((std::istreambuf_iterator<char>(f)), {});
		std::vector<std::string> pats;
		std::size_t off = 0;
		while (off < in.size() && pats.size() < 32) {
			auto e = in.find('\n', off);
			pats.push_back(in.substr(off, e == std::string::npos ? e : e - off));
			if (e == std::string::npos) { break; }
			off = e + 1;
		}
		std::vector<std::string> paths = pats;
		paths.push_back(in);
		if (!same(pats, paths)) { std::printf("DIFF on %s\n", argv[i]); return 1; }
	}
	std::printf("identical on %zu lookups\n", checked);
}
