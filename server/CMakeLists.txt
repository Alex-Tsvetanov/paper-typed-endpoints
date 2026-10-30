# The end-to-end arms (design note sections 3 and 5). Needs the pinned protobuf runtime and protoc
# under TEP_PROTOBUF_ROOT (bench/build_protobuf.sh) and the reflection layer.
if(NOT TEP_HAVE_REFLECTION)
  message(FATAL_ERROR "TEP_E2E needs the reflection layer (arm R)")
endif()
set(TEP_PROTOC ${TEP_PROTOBUF_ROOT}/protoc/bin/protoc${CMAKE_EXECUTABLE_SUFFIX})
if(NOT EXISTS ${TEP_PROTOC})
  message(FATAL_ERROR "TEP_PROTOBUF_ROOT (${TEP_PROTOBUF_ROOT}) has no protoc/bin/protoc; see bench/build_protobuf.sh")
endif()
list(APPEND CMAKE_PREFIX_PATH ${TEP_PROTOBUF_ROOT})
find_package(protobuf CONFIG REQUIRED)

set(TEP_E2E_GEN ${CMAKE_CURRENT_BINARY_DIR}/gen)
file(MAKE_DIRECTORY ${TEP_E2E_GEN})
add_custom_command(
  OUTPUT ${TEP_E2E_GEN}/e2e.pb.cc ${TEP_E2E_GEN}/e2e.pb.h
  COMMAND ${TEP_PROTOC} --cpp_out=${TEP_E2E_GEN} -I ${CMAKE_CURRENT_SOURCE_DIR} e2e.proto
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/e2e.proto
  COMMENT "protoc e2e.proto")

# protoc's output, built with the same flags and without the project's warning set.
add_library(e2e_pb STATIC ${TEP_E2E_GEN}/e2e.pb.cc)
target_include_directories(e2e_pb SYSTEM PUBLIC ${TEP_E2E_GEN})
target_link_libraries(e2e_pb PUBLIC protobuf::libprotobuf)

add_library(e2e_arms STATIC arm_r.cpp arm_p.cpp requests.cpp)
target_include_directories(e2e_arms PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(e2e_arms PUBLIC tep_reflect e2e_pb)
tep_test_target(e2e_arms)

add_executable(test_arms test_arms.cpp)
target_include_directories(test_arms PRIVATE ${PROJECT_SOURCE_DIR}/tests)
target_link_libraries(test_arms PRIVATE e2e_arms)
tep_test_target(test_arms)
add_test(NAME arms COMMAND test_arms)

# The transport and the benchmark server. libnghttp2 is the pinned static build under
# TEP_NGHTTP2_ROOT (bench/build_nghttp2.sh); without it, on Linux only, the system's (pkg-config).
if(TEP_NGHTTP2_ROOT)
  list(APPEND CMAKE_PREFIX_PATH ${TEP_NGHTTP2_ROOT})
  find_package(nghttp2 1.70.0 EXACT CONFIG REQUIRED)
  set(TEP_NGHTTP2 nghttp2::nghttp2_static)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(NGHTTP2 REQUIRED IMPORTED_TARGET libnghttp2)
  set(TEP_NGHTTP2 PkgConfig::NGHTTP2)
else()
  message(FATAL_ERROR "TEP_E2E on ${CMAKE_SYSTEM_NAME} needs TEP_NGHTTP2_ROOT; see bench/build_nghttp2.sh")
endif()
find_package(Threads REQUIRED)
if(WIN32)
  add_library(e2e_transport STATIC h2_server.cpp net_windows.cpp)
  target_link_libraries(e2e_transport PUBLIC ws2_32)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_library(e2e_transport STATIC h2_server.cpp net_linux.cpp)
else()
  message(FATAL_ERROR "The transport has a Linux (epoll) and a Windows (WSAPoll) back end only")
endif()
target_include_directories(e2e_transport PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(e2e_transport PUBLIC e2e_arms ${TEP_NGHTTP2} Threads::Threads)
tep_test_target(e2e_transport)

add_executable(tep_server main.cpp)
target_link_libraries(tep_server PRIVATE e2e_transport)
tep_test_target(tep_server)

add_executable(test_server test_server.cpp)
target_include_directories(test_server PRIVATE ${PROJECT_SOURCE_DIR}/tests)
target_link_libraries(test_server PRIVATE e2e_transport)
tep_test_target(test_server)
add_test(NAME server COMMAND test_server)
