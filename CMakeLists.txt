# radix-router: a high-performance path router (radix tree) for C++.
#
#   cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build && ctest --test-dir build
#   ./build/radix_router_bench      # linear-scan vs radix-tree lookup throughput
#
# Header-only (radix_router.h). The test and benchmark build only when this repo
# is the top-level project, so consumers pulling it in via FetchContent don't.

cmake_minimum_required(VERSION 3.16)
project(radix_router LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE Release)
endif()

add_library(radix_router INTERFACE)
target_include_directories(radix_router INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(radix_router INTERFACE cxx_std_20)
add_library(radix_router::radix_router ALIAS radix_router)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
	enable_testing()
	add_executable(radix_router_test test/test.cc)
	target_link_libraries(radix_router_test PRIVATE radix_router)
	add_test(NAME radix_router COMMAND radix_router_test)

	# A runnable benchmark (not a test): ./build/radix_router_bench
	add_executable(radix_router_bench examples/bench.cc)
	target_link_libraries(radix_router_bench PRIVATE radix_router)

	# Comparison benchmark (not run by ctest): radix::Router against a hash map of exact routes and
	# a segment trie on a hash map. The map is abseil's flat_hash_map, fetched at a pinned release
	# for this target only; it is never a dependency of the library target. With the option on, the
	# program also runs in `--check` mode under ctest (results, no timing). Configure with
	# -DCMAKE_BUILD_TYPE=Release.
	option(RADIX_ROUTER_BUILD_BENCH "Build bench/compare.cc (fetches abseil)" OFF)
	if(RADIX_ROUTER_BUILD_BENCH)
		include(FetchContent)
		set(ABSL_PROPAGATE_CXX_STD ON CACHE BOOL "" FORCE)
		set(ABSL_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
		FetchContent_Declare(abseil
			GIT_REPOSITORY https://github.com/abseil/abseil-cpp.git
			GIT_TAG        20260817.0)
		FetchContent_MakeAvailable(abseil)
		add_executable(radix_router_compare bench/compare.cc)
		target_link_libraries(radix_router_compare PRIVATE radix_router absl::flat_hash_map)
		target_compile_definitions(radix_router_compare PRIVATE HAVE_ABSL=1)
		add_test(NAME radix_router_compare COMMAND radix_router_compare --check)
	endif()
endif()
