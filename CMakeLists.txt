cmake_minimum_required(VERSION 3.28)
project(typed_endpoints VERSION 0.1.0 LANGUAGES CXX)

if(MSVC)
  # CMake 4.3 knows no C++26 flag for MSVC; the portable layers need C++23 only.
  set(CMAKE_CXX_STANDARD 23)
else()
  set(CMAKE_CXX_STANDARD 26)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# No C++ modules here; scanning would add -fmodules-ts and a scan step to every compile.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

option(TEP_BUILD_TESTS "Build the functional tests" ON)
option(TEP_WERROR "Treat warnings as errors" ON)
option(TEP_FUZZ "Also build the fuzz targets as libFuzzer executables (clang only)" OFF)
option(TEP_E2E "Build the end-to-end arms (needs the reflection layer and the pinned protobuf)" OFF)
set(TEP_PROTOBUF_ROOT "" CACHE PATH "Install prefix of the pinned protobuf (bench/build_protobuf.sh)")
set(TEP_NGHTTP2_ROOT "" CACHE PATH "Install prefix of the pinned libnghttp2 (bench/build_nghttp2.sh); empty: pkg-config (Linux)")

# Portable layers: schema front end, wire primitives, gRPC framing (C++23).
add_library(tep INTERFACE)
add_library(tep::tep ALIAS tep)
target_include_directories(tep INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(tep INTERFACE cxx_std_23)

# Reflection layer (P2996). The compiler must define __cpp_impl_reflection; GCC 16 needs
# -freflection. There is no fallback: without reflection the layer is not built at all.
include(CheckCXXSourceCompiles)
set(TEP_REFLECTION_FLAGS "")
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  set(TEP_REFLECTION_FLAGS -freflection)
endif()
set(CMAKE_REQUIRED_FLAGS "${TEP_REFLECTION_FLAGS}")
check_cxx_source_compiles([[
#include <meta>
#ifndef __cpp_impl_reflection
#error no reflection
#endif
int main() { return 0; }
]] TEP_HAVE_REFLECTION)
unset(CMAKE_REQUIRED_FLAGS)

if(TEP_HAVE_REFLECTION)
  add_library(tep_reflect INTERFACE)
  add_library(tep::reflect ALIAS tep_reflect)
  target_link_libraries(tep_reflect INTERFACE tep)
  target_compile_options(tep_reflect INTERFACE ${TEP_REFLECTION_FLAGS})
  message(STATUS "tep: reflection layer built with ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
else()
  message(STATUS "tep: reflection layer NOT built: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION} has no P2996")
endif()

if(TEP_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
  add_subdirectory(fuzz)
  if(TEP_E2E)
    add_subdirectory(server)
  endif()
endif()
