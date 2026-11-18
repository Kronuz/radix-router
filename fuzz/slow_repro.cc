// Times matching of the retained slow fuzz inputs and the nested catch-all family.
#include "radix_router.h"
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
int main(int argc, char** argv) {
	using clk = std::chrono::steady_clock;
	auto secs = [](clk::time_point a) { return std::chrono::duration<double>(clk::now() - a).count(); };
	// Family: k nested non-final catch-alls against n repeated "/x" segments, no final "/z".
	for (int k : {2, 4, 6, 8}) {
		std::string pat;
		for (int i = 0; i < k; ++i) { pat += "/*c" + std::to_string(i); }
		pat += "/z";
		radix::Router<int> r;
		r.insert(pat, 1);
		for (int n : {8, 16, 32, 64, 4096}) {
			std::string path;
			for (int i = 0; i < n; ++i) { path += "/x"; }
			radix::Params p;
			auto t = clk::now();
			r.find(path, p);
			std::printf("family k=%d n=%d %.6fs\n", k, n, secs(t));
		}
	}
	for (int i = 1; i < argc; ++i) {
		std::ifstream f(argv[i], std::ios::binary);
		std::string in((std::istreambuf_iterator<char>(f)), {});
		auto t = clk::now();
		radix::Router<std::size_t> r;
		std::size_t off = 0, idx = 0;
		while (off < in.size() && idx < 32) {
			auto e = in.find('\n', off);
			try { r.insert(in.substr(off, e == std::string::npos ? e : e - off), idx++); } catch (const std::invalid_argument&) {}
			if (e == std::string::npos) { break; }
			off = e + 1;
		}
		radix::Params p;
		r.find(in, p);
		std::printf("artifact %s %.6fs\n", argv[i], secs(t));
	}
}
