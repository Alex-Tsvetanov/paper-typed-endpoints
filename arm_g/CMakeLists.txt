# Arm G (M2, hypotheses-m2.md): the end-to-end echo service served by gRPC C++ 1.84.2. A project of
# its own, configured from this folder, so that the measured code of arms R, P, Q and S (include/,
# server/, the root CMake files) stays as the first campaign and the replication built it. It
# reads server/e2e.proto and server/requests.cpp and changes nothing outside this folder.
# TEP_GRPC_ROOT is a prefix made by bench/build_grpc.sh (gRPC, protobuf with libprotoc, abseil,
# c-ares, re2), with the pinned protoc release binary under protoc/bin.
cmake_minimum_required(VERSION 3.28)
project(tep_arm_g LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

set(TEP_GRPC_ROOT "" CACHE PATH "Install prefix of bench/build_grpc.sh")
if(NOT EXISTS "${TEP_GRPC_ROOT}/bin/grpc_cpp_plugin" OR NOT EXISTS "${TEP_GRPC_ROOT}/protoc/bin/protoc")
  message(FATAL_ERROR "TEP_GRPC_ROOT (${TEP_GRPC_ROOT}) has no bin/grpc_cpp_plugin or protoc/bin/protoc; see bench/build_grpc.sh")
endif()
list(APPEND CMAKE_PREFIX_PATH ${TEP_GRPC_ROOT})
find_package(protobuf CONFIG REQUIRED)
find_package(gRPC 1.84.2 EXACT CONFIG REQUIRED)
message(STATUS "tep: arm G on gRPC ${gRPC_VERSION}, protobuf ${protobuf_VERSION}")

set(server_dir ${CMAKE_CURRENT_SOURCE_DIR}/../server)
set(gen ${CMAKE_CURRENT_BINARY_DIR}/gen)
file(MAKE_DIRECTORY ${gen})
add_custom_command(
  OUTPUT ${gen}/e2e.pb.cc ${gen}/e2e.pb.h ${gen}/e2e.grpc.pb.cc ${gen}/e2e.grpc.pb.h
  COMMAND ${TEP_GRPC_ROOT}/protoc/bin/protoc --cpp_out=${gen} --grpc_out=${gen}
          --plugin=protoc-gen-grpc=${TEP_GRPC_ROOT}/bin/grpc_cpp_plugin -I ${server_dir} e2e.proto
  DEPENDS ${server_dir}/e2e.proto
  COMMENT "protoc and grpc_cpp_plugin: e2e.proto")

# protoc's and grpc_cpp_plugin's output, without the warning set.
add_library(g_pb STATIC ${gen}/e2e.pb.cc ${gen}/e2e.grpc.pb.cc)
target_include_directories(g_pb SYSTEM PUBLIC ${gen})
target_link_libraries(g_pb PUBLIC gRPC::grpc++ protobuf::libprotobuf)

add_executable(g_server g_server.cpp)
target_link_libraries(g_server PRIVATE g_pb)
target_compile_options(g_server PRIVATE -Wall -Wextra -Werror)

# Starts g_server, checks every reply byte for byte against its request, from one and from four
# threads, stops it, and checks its exit status and its count of calls.
add_executable(test_g test_g.cpp ${server_dir}/requests.cpp)
target_include_directories(test_g PRIVATE ${server_dir})
target_link_libraries(test_g PRIVATE g_pb)
target_compile_options(test_g PRIVATE -Wall -Wextra -Werror)
target_compile_definitions(test_g PRIVATE G_SERVER="$<TARGET_FILE:g_server>")
enable_testing()
add_test(NAME g_server COMMAND test_g)
set_tests_properties(g_server PROPERTIES TIMEOUT 300)
