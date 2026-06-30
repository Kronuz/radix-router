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
endif()
