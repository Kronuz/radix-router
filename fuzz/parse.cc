#include "radix_router.h"
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
	const std::string input(reinterpret_cast<const char*>(data), size);
	std::vector<std::string> patterns;
	std::size_t offset = 0;
	while (offset < input.size() && patterns.size() < 32) {
		const auto end = input.find('\n', offset);
		patterns.emplace_back(input.substr(offset, end == std::string::npos ? end : end - offset));
		if (end == std::string::npos) { break; }
		offset = end + 1;
	}
	radix::Router<std::size_t> router;
	for (std::size_t i = 0; i < patterns.size(); ++i) {
		try { router.insert(patterns[i], i); }
		catch (const std::invalid_argument&) {}
	}
	// Fixed overlapping routes exercise wildcard backtracking on arbitrary paths.
	radix::Router<std::size_t> overlap;
	overlap.insert("/users/new", 0);
	overlap.insert("/users/:id", 1);
	overlap.insert("/files/*path/raw", 2);
	overlap.insert("/files/:name", 3);
	radix::Params parameters;
	const auto lookup = [&](const auto& table, std::string_view path) {
		parameters.clear();
		const auto* result = table.find(path, parameters);
		if (result != table.find(path)) { std::abort(); }
		if (result) {
			for (const auto& [name, value] : parameters) {
				(void)name;
				if (value.size() > path.size()) { std::abort(); }
			}
		}
	};
	lookup(router, input);
	lookup(overlap, input);
	for (const auto& path : patterns) { lookup(router, path); lookup(overlap, path); }
	return 0;
}
