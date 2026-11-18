// A/B of a typical routing table and path mix: built with -DNS=radix (current) or -DNS=radix_main
// (main's matcher, fuzz/radix_router_main.h). Prints ns per lookup, best of 7 rounds.
#include HDR
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

int main() {
	NS::Router<int> r;
	int v = 0;
	for (const char* p : {"/", "/status", "/search", "/users/new", "/users/:id", "/users/:id/posts/:pid", "/users/:id/edit",
	                      "/documents", "/documents/count", "/static/*path", "/*ns/", "/*ns/:id", "/files/*path/raw",
	                      "/api/v1/items", "/api/v1/items/:id", "/api/v1/items/:id/tags", "/api/v2/items/:id"}) {
		r.insert(p, v++);
	}
	std::vector<std::string> paths = {"/", "/status", "/users/new", "/users/42", "/users/42/posts/7", "/users/42/edit",
	                                  "/documents/count", "/static/css/app.css", "/a/b/c/", "/a/b/c/42", "/files/a/b/c/raw",
	                                  "/api/v1/items/9/tags", "/nope/x/y/z/w", "/files/a/b/c/d/e/f/g/meta"};
	double best = 1e18;
	for (int round = 0; round < 7; ++round) {
		NS::Params p;
		std::size_t sink = 0;
		auto t0 = std::chrono::steady_clock::now();
		for (int i = 0; i < 400000; ++i) {
			for (const auto& path : paths) { sink += r.find(path, p) != nullptr; }
		}
		double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count() / (400000.0 * paths.size());
		if (ns < best) { best = ns; }
		if (sink == 1) { std::puts(""); }
	}
	std::printf("%.2f ns/lookup\n", best);
}
