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
// looked-up path) and independent of how many patterns are registered. The naive
// alternative -- scanning every registered pattern -- is O(patterns); bench.cc
// measures the gap.
//
// Transport-agnostic: it maps '/'-separated path patterns to a value of any type
// T and knows nothing about HTTP. A pattern is a sequence of segments:
//   * static, e.g. "/documents/count"
//   * ":name" param    -- matches exactly one non-empty segment (up to the next '/')
//   * "*name" catch-all -- matches one or more segments. As the FINAL segment it is
//     the classic catch-all, consuming the whole remainder (possibly empty). In a
//     NON-final position ("/*ns/:id", "/files/*path/raw") it matches a run of
//     segments chosen so the rest of the pattern still matches -- a greedy prefix
//     capture. Matched ":name"/"*name" values are reported through Params.
//
// Matching is priority-ordered at every node -- static beats param beats catch-all
// -- with bounded backtracking: a wildcard-free lookup is a straight O(path) descent
// with no backtracking (the fast path); backtracking is entered only where a node
// actually carries a wildcard alternative beside a more specific one, and is bounded
// by the number of path segments. That is what lets overlapping routes coexist --
// "/users/new" beside "/users/:id", or the non-final catch-all above -- which the
// stricter httprouter model (static XOR wildcard per node) forbids. Registering the
// *same* pattern twice, or a malformed pattern, is a programming error and throws at
// insert -- never a per-lookup cost.

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
	// Drop everything captured after index n (undo a failed backtracking branch).
	void truncate(std::size_t n) { items_.resize(n); }
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
	// pattern or on registering the same pattern twice (both are programming
	// errors, caught at setup rather than per lookup).
	void insert(std::string_view pattern, T value) {
		if (pattern.empty()) { throw std::invalid_argument("pattern must not be empty"); }
		root_.add_route(pattern, std::move(value));
	}

	// Look `path` up. Returns a pointer to the registered value, or nullptr if no
	// pattern matches. Captured params are reported through `params` (cleared first).
	const T* find(std::string_view path, Params& params) const {
		params.clear();
		return root_.match(path, params);
	}

	// Convenience overload when the caller does not need the captured params.
	const T* find(std::string_view path) const {
		Params discard;
		return find(path, discard);
	}

private:
	enum class NType : unsigned char { Static, Param, CatchAll };

	struct Node {
		NType type = NType::Static;
		std::string path;                                 // Static: the compressed edge text ("/users/"). Param/CatchAll: empty.
		std::string name;                                 // Param/CatchAll: the capture name (":id" -> "id"). Static: empty.
		std::string indices;                              // first byte of each static child, parallel to statics
		std::vector<std::unique_ptr<Node>> statics;
		std::unique_ptr<Node> param;                      // ":name" alternative at this position
		std::unique_ptr<Node> catchall;                   // "*name" alternative at this position
		bool catchall_multi = false;                      // catch-all matches 1+ segments then a continuation (vs. final)
		std::optional<T> value;                           // set => a pattern ends here

		static std::size_t common_prefix(std::string_view a, std::string_view b) {
			std::size_t max = std::min(a.size(), b.size());
			std::size_t i = 0;
			while (i < max && a[i] == b[i]) { ++i; }
			return i;
		}

		// ---- registration ------------------------------------------------
		// Consume the leading static run of `pattern` (up to the first ':' or '*' or
		// the end), descending / edge-splitting static children from `this`. Returns
		// the node the run ends at; `pattern` is advanced past the consumed run.
		Node* descend_static(std::string_view& pattern) {
			Node* n = this;
			for (;;) {
				std::size_t w = pattern.size();
				for (std::size_t k = 0; k < pattern.size(); ++k) {
					if (pattern[k] == ':' || pattern[k] == '*') { w = k; break; }
				}
				std::string_view run = pattern.substr(0, w);
				if (run.empty()) { return n; }   // pattern begins with a wildcard (or is empty)

				// n->path is a static edge only for a Static node; for Param/CatchAll it
				// is empty, so the run drops straight into a static child of n.
				std::size_t i = common_prefix(run, n->path);
				if (i < n->path.size()) {  // split n's edge: its divergent tail becomes a static child
					auto child = std::make_unique<Node>();
					child->type = NType::Static;
					child->path = n->path.substr(i);
					child->indices.swap(n->indices);
					child->statics.swap(n->statics);
					child->param.swap(n->param);
					child->catchall.swap(n->catchall);
					child->catchall_multi = n->catchall_multi;
					child->value.swap(n->value);
					n->catchall_multi = false;
					n->indices.assign(1, child->path[0]);
					n->statics.push_back(std::move(child));
					n->path.assign(n->path.substr(0, i));
				}
				if (i < run.size()) {   // more static text than n's edge holds: go to / create a child
					run = run.substr(i);
					pattern = pattern.substr(i);
					char c = run.front();
					bool descended = false;
					for (std::size_t k = 0; k < n->indices.size(); ++k) {
						if (n->indices[k] == c) { n = n->statics[k].get(); descended = true; break; }
					}
					if (descended) { continue; }
					n->indices.push_back(c);
					auto child = std::make_unique<Node>();
					child->type = NType::Static;
					child->path.assign(run);
					Node* cp = child.get();
					n->statics.push_back(std::move(child));
					pattern = pattern.substr(run.size());
					return cp;
				}
				pattern = pattern.substr(run.size());
				return n;
			}
		}

		void add_route(std::string_view pattern, T value_in) {
			Node* n = this;
			for (;;) {
				n = n->descend_static(pattern);
				if (pattern.empty()) {
					if (n->value) { throw std::invalid_argument("a value is already registered for this pattern"); }
					n->value = std::move(value_in);
					return;
				}
				char sigil = pattern.front();
				std::size_t j = 1;
				while (j < pattern.size() && pattern[j] != '/') {
					if (pattern[j] == ':' || pattern[j] == '*') { throw std::invalid_argument("only one wildcard per path segment is allowed"); }
					++j;
				}
				if (j == 1) { throw std::invalid_argument("wildcards must be named"); }
				std::string_view wname = pattern.substr(1, j - 1);
				bool final = (j == pattern.size());

				if (sigil == ':') {
					if (!n->param) {
						n->param = std::make_unique<Node>();
						n->param->type = NType::Param;
						n->param->name.assign(wname);
					} else if (n->param->name != wname) {
						throw std::invalid_argument("conflicting param names for the same path position");
					}
					n = n->param.get();
					pattern = pattern.substr(j);
					continue;
				}

				// catch-all
				if (!n->catchall) {
					n->catchall = std::make_unique<Node>();
					n->catchall->type = NType::CatchAll;
					n->catchall->name.assign(wname);
					n->catchall->catchall_multi = !final;
				} else {
					if (n->catchall->name != wname) { throw std::invalid_argument("conflicting catch-all names for the same path position"); }
					if (n->catchall->catchall_multi == final) { throw std::invalid_argument("catch-all is both final and non-final at the same position"); }
				}
				n = n->catchall.get();
				pattern = pattern.substr(j);
				if (final) {
					if (n->value) { throw std::invalid_argument("a value is already registered for this pattern"); }
					n->value = std::move(value_in);
					return;
				}
				continue;   // non-final catch-all: a continuation follows
			}
		}

		// ---- lookup -------------------------------------------------------
		// Enter this node (consume its static edge, or capture a segment/run), then
		// dispatch its children on the remainder. Returns the matched value, or
		// nullptr to make the caller backtrack.
		const T* match(std::string_view path, Params& params) const {
			switch (type) {
				case NType::Static:
					if (path.size() < this->path.size() || path.compare(0, this->path.size(), this->path) != 0) {
						return nullptr;
					}
					return dispatch(path.substr(this->path.size()), params);

				case NType::Param: {
					std::size_t end = 0;
					while (end < path.size() && path[end] != '/') { ++end; }
					if (end == 0) { return nullptr; }   // a param needs a non-empty segment
					std::size_t saved = params.size();
					params.add(name, path.substr(0, end));
					if (const T* hit = dispatch(path.substr(end), params)) { return hit; }
					params.truncate(saved);
					return nullptr;
				}

				case NType::CatchAll:
					if (!catchall_multi) {   // final catch-all: consume the whole remainder
						params.add(name, path);
						return value ? &*value : nullptr;
					}
					// non-final: capture 1+ segments, backtracking, then the continuation.
					for (std::size_t pos = 0;;) {
						std::size_t seg_end = pos;
						while (seg_end < path.size() && path[seg_end] != '/') { ++seg_end; }
						if (seg_end == pos) { break; }   // no further segment to consume
						pos = seg_end;
						std::size_t saved = params.size();
						params.add(name, path.substr(0, pos));
						if (const T* hit = dispatch(path.substr(pos), params)) { return hit; }
						params.truncate(saved);
						if (pos < path.size() && path[pos] == '/') { ++pos; } else { break; }
					}
					return nullptr;
			}
			return nullptr;
		}

		// After a node consumed its own text/capture, try its children on `path` in
		// priority order: static (exact first byte) > param > catch-all. The single
		// point where bounded backtracking branches.
		const T* dispatch(std::string_view path, Params& params) const {
			if (path.empty()) { return value ? &*value : nullptr; }
			char c = path.front();
			for (std::size_t k = 0; k < indices.size(); ++k) {
				if (indices[k] == c) {
					if (const T* hit = statics[k]->match(path, params)) { return hit; }
					break;   // at most one static child starts with c; fall through to the wildcards
				}
			}
			if (param) {
				if (const T* hit = param->match(path, params)) { return hit; }
			}
			if (catchall) {
				if (const T* hit = catchall->match(path, params)) { return hit; }
			}
			return nullptr;
		}
	};

	Node root_;
};

}  // namespace radix
