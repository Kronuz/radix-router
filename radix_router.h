/*
 * Copyright (c) 2026 Germán Méndez Bravo (Kronuz)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

// radix::Router<T> -- a high-performance path router built on a radix tree.
//
// A radix tree (a compressed prefix trie) is the structure battle-tested by
// julienschmidt/httprouter, on which Gin, Echo and Fiber are built. A lookup
// walks the tree matching shared path prefixes, so its cost is O(length of the
// looked-up path) and is independent of how many patterns are registered. The
// naive alternative -- scanning every registered pattern -- is O(patterns) and
// degrades as the table grows; examples/bench.cc measures the gap.
//
// This library is deliberately transport-agnostic: it maps '/'-separated path
// patterns to a value of any type T. It knows nothing about HTTP. A pattern is a
// sequence of segments:
//   * static, e.g. "/documents/count"
//   * ":name" param  -- matches exactly one segment (up to the next '/')
//   * "*name" catch-all -- matches the rest of the path, only as the final segment
// Matched ":name"/"*name" values are reported through Params at lookup time.
//
// As in httprouter, a node holds either static children or a single wildcard
// child, never both: registering "/users/new" and "/users/:id" at the same
// position is a conflict and throws at insertion time -- a programming error,
// surfaced early, not a per-lookup cost.

#pragma once

#include <algorithm>
#include <cstddef>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace radix {

// Path parameters captured during a lookup. Names are views into the registered
// pattern (stable for the router's lifetime); values are views into the path
// passed to find() (valid until that path buffer changes). Both are zero-copy.
class Params {
public:
	void add(std::string_view name, std::string_view value) { items_.emplace_back(name, value); }

	std::string_view get(std::string_view name) const {
		for (const auto& [k, v] : items_) {
			if (k == name) { return v; }
		}
		return {};
	}

	void clear() { items_.clear(); }
	bool empty() const { return items_.empty(); }
	std::size_t size() const { return items_.size(); }
	const std::pair<std::string_view, std::string_view>& operator[](std::size_t i) const { return items_[i]; }

	auto begin() const { return items_.begin(); }
	auto end() const { return items_.end(); }

private:
	std::vector<std::pair<std::string_view, std::string_view>> items_;
};


template <typename T>
class Router {
public:
	// Register `value` for `pattern`. Throws std::invalid_argument on a malformed
	// pattern or a conflict with an already-registered pattern (both are
	// programming errors, caught at setup rather than per lookup).
	void insert(std::string_view pattern, T value) {
		if (pattern.empty()) { throw std::invalid_argument("pattern must not be empty"); }
		root_.add_route(pattern, std::move(value));
	}

	// Look `path` up. Returns a pointer to the registered value, or nullptr if no
	// pattern matches. Captured params are appended to `params` (cleared first).
	const T* find(std::string_view path, Params& params) const {
		params.clear();
		return root_.find(path, params);
	}

	// Convenience overload when the caller does not need the captured params.
	const T* find(std::string_view path) const {
		Params discard;
		return find(path, discard);
	}

private:
	enum class NType : unsigned char { Static, Param, CatchAll };

	struct Node {
		std::string path;                                 // segment text at this node ("/users/", ":id", "*rest")
		std::string indices;                              // first byte of each static child, parallel to children
		std::vector<std::unique_ptr<Node>> children;
		std::optional<T> value;                           // set => a pattern ends here
		NType type = NType::Static;
		bool wild_child = false;                          // children.back() is a Param/CatchAll node

		static std::size_t common_prefix(std::string_view a, std::string_view b) {
			std::size_t max = std::min(a.size(), b.size());
			std::size_t i = 0;
			while (i < max && a[i] == b[i]) { ++i; }
			return i;
		}

		// ---- registration ------------------------------------------------
		void add_route(std::string_view pattern, T value_in) {
			Node* n = this;
			if (n->path.empty() && n->children.empty() && !n->wild_child) {  // empty tree
				n->insert_child(pattern, std::move(value_in));
				return;
			}
			for (;;) {
				std::size_t i = common_prefix(pattern, n->path);

				if (i < n->path.size()) {  // split this edge: push the divergent tail into a new child
					auto child = std::make_unique<Node>();
					child->path = n->path.substr(i);
					child->indices.swap(n->indices);
					child->children.swap(n->children);
					child->value.swap(n->value);
					child->wild_child = n->wild_child;
					child->type = NType::Static;

					n->wild_child = false;
					n->indices.assign(1, child->path[0]);
					n->children.clear();
					n->children.push_back(std::move(child));
					n->path.assign(pattern.substr(0, i));
					n->value.reset();
				}

				if (i < pattern.size()) {
					pattern = pattern.substr(i);

					if (n->wild_child) {  // must reuse the existing wildcard at this position
						n = n->children.back().get();
						std::size_t seg = 0;
						while (seg < pattern.size() && pattern[seg] != '/') { ++seg; }
						if (seg != n->path.size() || pattern.compare(0, n->path.size(), n->path) != 0) {
							throw std::invalid_argument("pattern conflicts with an existing wildcard segment");
						}
						continue;
					}

					char c = pattern.front();
					bool descended = false;
					for (std::size_t k = 0; k < n->indices.size(); ++k) {
						if (n->indices[k] == c) { n = n->children[k].get(); descended = true; break; }
					}
					if (descended) { continue; }

					if (c != ':' && c != '*') {  // brand-new static branch
						n->indices.push_back(c);
						auto child = std::make_unique<Node>();
						Node* cp = child.get();
						n->children.push_back(std::move(child));
						n = cp;
					}
					n->insert_child(pattern, std::move(value_in));
					return;
				}

				if (n->value) {
					throw std::invalid_argument("a value is already registered for this pattern");
				}
				n->value = std::move(value_in);
				return;
			}
		}

		// Build the chain of nodes for a pattern that may contain wildcards,
		// starting at (an otherwise fresh) `this`.
		void insert_child(std::string_view pattern, T value_in) {
			Node* n = this;
			for (;;) {
				std::size_t i = std::string_view::npos;
				char wt = 0;
				for (std::size_t k = 0; k < pattern.size(); ++k) {
					if (pattern[k] == ':' || pattern[k] == '*') { i = k; wt = pattern[k]; break; }
				}
				if (i == std::string_view::npos) {  // purely static tail
					n->path.assign(pattern);
					n->value = std::move(value_in);
					return;
				}

				std::size_t j = i + 1;
				while (j < pattern.size() && pattern[j] != '/') {
					if (pattern[j] == ':' || pattern[j] == '*') {
						throw std::invalid_argument("only one wildcard per path segment is allowed");
					}
					++j;
				}
				if (j == i + 1) { throw std::invalid_argument("wildcards must be named"); }

				if (i > 0) {  // static prefix becomes this node; wildcard becomes its child
					n->path.assign(pattern.substr(0, i));
					pattern = pattern.substr(i);
					j -= i;
					i = 0;
				}

				if (wt == ':') {
					auto child = std::make_unique<Node>();
					child->type = NType::Param;
					child->path.assign(pattern.substr(0, j));  // ":name"
					Node* cp = child.get();
					n->wild_child = true;
					n->children.push_back(std::move(child));
					n = cp;

					if (j < pattern.size()) {  // more path follows the param
						pattern = pattern.substr(j);
						auto rest = std::make_unique<Node>();
						Node* rp = rest.get();
						n->children.push_back(std::move(rest));
						n = rp;
						continue;
					}
					n->value = std::move(value_in);
					return;
				}

				// catch-all
				if (j != pattern.size()) { throw std::invalid_argument("catch-all must be the final path segment"); }
				if (i > 0 && pattern[i - 1] != '/') { throw std::invalid_argument("catch-all must follow '/'"); }
				auto child = std::make_unique<Node>();
				child->type = NType::CatchAll;
				child->path.assign(pattern);  // "*name"
				child->value = std::move(value_in);
				n->wild_child = true;
				n->children.push_back(std::move(child));
				return;
			}
		}

		// ---- lookup -------------------------------------------------------
		const T* find(std::string_view path, Params& params) const {
			const Node* n = this;
			for (;;) {
				if (path.size() > n->path.size()) {
					if (path.compare(0, n->path.size(), n->path) != 0) { return nullptr; }
					path = path.substr(n->path.size());

					char c = path.front();
					for (std::size_t k = 0; k < n->indices.size(); ++k) {
						if (n->indices[k] == c) { n = n->children[k].get(); goto next; }
					}
					if (!n->wild_child) { return nullptr; }

					n = n->children.back().get();
					if (n->type == NType::Param) {
						std::size_t end = 0;
						while (end < path.size() && path[end] != '/') { ++end; }
						params.add(std::string_view(n->path).substr(1), path.substr(0, end));
						if (end < path.size()) {  // descend past this segment
							if (n->children.empty()) { return nullptr; }
							path = path.substr(end);
							n = n->children.front().get();
							goto next;
						}
						return n->value ? &*n->value : nullptr;
					}
					// catch-all consumes the remainder
					params.add(std::string_view(n->path).substr(1), path);
					return n->value ? &*n->value : nullptr;
				}

				if (path == n->path) { return n->value ? &*n->value : nullptr; }
				return nullptr;

				next:;
			}
		}
	};

	Node root_;
};

}  // namespace radix
