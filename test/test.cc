/*
 * Correctness tests for radix::Router<T>: static and wildcard matching, parameter
 * capture, edge splitting, catch-all, and the conflict checks that fire at
 * insertion time. Run via ctest (radix_router_test).
 *
 * A radix tree is easy to get subtly wrong (prefix splitting, catch-all, and the
 * priority-ordered backtracking that lets static, param and catch-all coexist at
 * one position), so these lean on the awkward shapes.
 */

#include "radix_router.h"

#include <cstdio>
#include <stdexcept>
#include <string>

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond)                                                                      \
	do {                                                                                 \
		++g_checks;                                                                      \
		if (!(cond)) {                                                                    \
			++g_failures;                                                                 \
			std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);                 \
		}                                                                                \
	} while (0)

using Router = radix::Router<std::string>;

// Resolve `path` and return "<value> a=<param a> ..." for asserting which pattern
// matched and what it captured; "<none>" if nothing matched.
static std::string resolve(const Router& r, const std::string& path, std::vector<std::string> names = {}) {
	radix::Params p;
	const std::string* v = r.find(path, p);
	if (v == nullptr) { return "<none>"; }
	std::string out = *v;
	for (const auto& n : names) {
		out += " ";
		out += n;
		out += "=";
		out += std::string(p.get(n));
	}
	return out;
}

static void test_static() {
	std::printf("static routing\n");
	Router r;
	r.insert("/", "root");
	r.insert("/documents", "docs");
	r.insert("/documents/count", "count");
	r.insert("/search", "search");
	r.insert("/status", "status");   // shares "/s" prefix with /search

	CHECK(resolve(r, "/") == "root");
	CHECK(resolve(r, "/documents") == "docs");
	CHECK(resolve(r, "/documents/count") == "count");
	CHECK(resolve(r, "/search") == "search");
	CHECK(resolve(r, "/status") == "status");
	CHECK(resolve(r, "/do") == "<none>");          // partial prefix is not a match
	CHECK(resolve(r, "/documentation") == "<none>");
}

static void test_params() {
	std::printf("param capture\n");
	Router r;
	r.insert("/users/:id", "user");
	r.insert("/users/:id/posts/:pid", "post");
	r.insert("/users/:id/edit", "edit");
	r.insert("/users/:id/delete", "del");   // splits the ":id" tail with /edit

	CHECK(resolve(r, "/users/42", {"id"}) == "user id=42");
	CHECK(resolve(r, "/users/42/posts/7", {"id", "pid"}) == "post id=42 pid=7");
	CHECK(resolve(r, "/users/42/edit", {"id"}) == "edit id=42");
	CHECK(resolve(r, "/users/42/delete", {"id"}) == "del id=42");
	CHECK(resolve(r, "/users/42/unknown") == "<none>");   // a param spans one segment only
	CHECK(resolve(r, "/users/") == "<none>");              // empty segment is not a param value? (see note)
}

static void test_value_and_param_at_same_node() {
	std::printf("exact pattern alongside a param child\n");
	Router r;
	r.insert("/files/", "list");          // exact
	r.insert("/files/:name", "file");

	CHECK(resolve(r, "/files/") == "list");
	CHECK(resolve(r, "/files/report.txt", {"name"}) == "file name=report.txt");
}

static void test_catch_all() {
	std::printf("catch-all\n");
	Router r;
	r.insert("/static/*path", "asset");

	CHECK(resolve(r, "/static/css/app.css", {"path"}) == "asset path=css/app.css");
	CHECK(resolve(r, "/static/x", {"path"}) == "asset path=x");
	CHECK(resolve(r, "/other") == "<none>");
}

static void test_deep_static_split() {
	std::printf("shared-prefix splitting\n");
	Router r;
	r.insert("/api/v1/users", "v1users");
	r.insert("/api/v1/posts", "v1posts");
	r.insert("/api/v2/users", "v2users");

	CHECK(resolve(r, "/api/v1/users") == "v1users");
	CHECK(resolve(r, "/api/v1/posts") == "v1posts");
	CHECK(resolve(r, "/api/v2/users") == "v2users");
	CHECK(resolve(r, "/api/v1/use") == "<none>");
	CHECK(resolve(r, "/api/v3/users") == "<none>");
}

template <typename F>
static bool throws_invalid_argument(F&& f) {
	try {
		f();
	} catch (const std::invalid_argument&) {
		return true;
	} catch (...) {
		return false;
	}
	return false;
}

static void test_conflicts() {
	std::printf("insertion conflicts throw\n");

	CHECK(throws_invalid_argument([] {
		Router r;
		r.insert("/u/:id", "a");
		r.insert("/u/:name", "b");      // differing param names, same position
	}));

	CHECK(throws_invalid_argument([] {
		Router r;
		r.insert("/dup", "a");
		r.insert("/dup", "b");          // duplicate value
	}));

	CHECK(throws_invalid_argument([] {
		Router r;
		r.insert("/a/*mid/b", "a");
		r.insert("/a/*mid", "b");       // same catch-all, both non-final and final
	}));

	CHECK(throws_invalid_argument([] {
		Router r;
		r.insert("/a/:", "a");          // unnamed wildcard
	}));

	CHECK(throws_invalid_argument([] {
		Router r;
		r.insert("/a/:x:y", "a");       // two wildcards in one segment
	}));
}

// The overlaps the stricter static-XOR-wildcard model forbids but bounded
// backtracking supports: a static route beside a param at the same position,
// resolved static-first.
static void test_static_beside_param() {
	std::printf("static route beside a param (priority + backtracking)\n");
	Router r;
	r.insert("/users/:id", "user");
	r.insert("/users/new", "new");        // static, same position as :id
	r.insert("/users/:id/edit", "edit");

	CHECK(resolve(r, "/users/new") == "new");              // static wins over param
	CHECK(resolve(r, "/users/42", {"id"}) == "user id=42"); // param still reached
	CHECK(resolve(r, "/users/42/edit", {"id"}) == "edit id=42");
	CHECK(resolve(r, "/users/new/edit", {"id"}) == "edit id=new"); // no /edit under static "new"; backtracks to :id/edit
}

// A catch-all in a NON-final position: it captures a greedy run of segments and
// still lets the fixed continuation match, with backtracking to find the split.
static void test_non_final_catch_all() {
	std::printf("non-final catch-all with a continuation\n");
	Router r;
	r.insert("/files/*path/raw", "raw");
	r.insert("/files/*path/meta", "meta");

	CHECK(resolve(r, "/files/a/raw", {"path"}) == "raw path=a");
	CHECK(resolve(r, "/files/a/b/c/raw", {"path"}) == "raw path=a/b/c");
	CHECK(resolve(r, "/files/a/b/meta", {"path"}) == "meta path=a/b");
	CHECK(resolve(r, "/files/a/b/raw") == "raw");          // continuation matched
	CHECK(resolve(r, "/files/raw") == "<none>");           // catch-all needs 1+ segments before /raw
	CHECK(resolve(r, "/files/a/b") == "<none>");           // no continuation present
}

// Xapiand's real shape: a variable-depth namespace (non-final catch-all) then an
// id, beside a trailing-slash collection form.
static void test_namespace_then_id() {
	std::printf("variable-depth namespace + id, collection vs document\n");
	Router r;
	r.insert("/*ns/", "collection");      // trailing slash => the whole path is a collection
	r.insert("/*ns/:id", "document");     // last segment is a document id

	CHECK(resolve(r, "/test/", {"ns"}) == "collection ns=test");
	CHECK(resolve(r, "/test/namespace/types/", {"ns"}) == "collection ns=test/namespace/types");
	CHECK(resolve(r, "/test/42", {"ns", "id"}) == "document ns=test id=42");
	CHECK(resolve(r, "/a/b/c/42", {"ns", "id"}) == "document ns=a/b/c id=42");
}

int main() {
	std::printf("== radix::Router tests ==\n");
	test_static();
	test_params();
	test_value_and_param_at_same_node();
	test_catch_all();
	test_deep_static_split();
	test_conflicts();
	test_static_beside_param();
	test_non_final_catch_all();
	test_namespace_then_id();

	std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}
