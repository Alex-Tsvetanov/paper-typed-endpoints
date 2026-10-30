# The end-to-end arms (design note sections 3 and 5) and the transport. Arm S, a stub that echoes
# the request bytes with no codec, is always built: the transport's own test arm and its lower bound.
# Arm P needs the pinned protobuf runtime and protoc under TEP_PROTOBUF_ROOT (bench/build_protobuf.sh);
# arm R also needs the reflection layer. TEP_E2E_TRANSPORT_ONLY builds the transport with arm S
# alone, without protobuf: the Windows sanitizer record (MSVC ASan cannot start protoc's generated
# code; Alex, 2026-10-30, design/revision-log.md).
if(TEP_E2E_TRANSPORT_ONLY)
  set(TEP_E2E_ARM_P 0)
  set(TEP_E2E_ARM_R 0)
  message(STATUS "tep: end-to-end transport only: arm S, no protobuf")
else()
  set(TEP_E2E_ARM_P 1)
  if(TEP_HAVE_REFLECTION)
    set(TEP_E2E_ARM_R 1)
  else()
    set(TEP_E2E_ARM_R 0)
    message(STATUS "tep: end-to-end without arm R (no P2996)")
  endif()
endif()

add_library(e2e_arms STATIC arm_s.cpp)
target_include_directories(e2e_arms PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(e2e_arms PUBLIC tep)
target_compile_definitions(e2e_arms PUBLIC TEP_E2E_ARM_P=${TEP_E2E_ARM_P} TEP_E2E_ARM_R=${TEP_E2E_ARM_R})
tep_test_target(e2e_arms)

if(TEP_E2E_ARM_P)
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

  target_sources(e2e_arms PRIVATE arm_p.cpp requests.cpp)
  target_link_libraries(e2e_arms PUBLIC e2e_pb)
  if(MSVC)
    # C4702 (unreachable code) is reported inside protobuf's arena.h when arm P instantiates it.
    set_source_files_properties(arm_p.cpp PROPERTIES COMPILE_OPTIONS /wd4702)
  endif()

  # The load generator's request bodies (framed small and medium requests), checked on the way out.
  add_executable(write_bodies write_bodies.cpp)
  target_link_libraries(write_bodies PRIVATE e2e_arms)
  tep_test_target(write_bodies)
  add_test(NAME bodies COMMAND write_bodies ${CMAKE_CURRENT_BINARY_DIR}/bodies)
endif()

if(TEP_E2E_ARM_R)
  target_sources(e2e_arms PRIVATE arm_r.cpp)
  target_link_libraries(e2e_arms PUBLIC tep_reflect)

  add_executable(test_arms test_arms.cpp)
  target_include_directories(test_arms PRIVATE ${PROJECT_SOURCE_DIR}/tests)
  target_link_libraries(test_arms PRIVATE e2e_arms)
  tep_test_target(test_arms)
  add_test(NAME arms COMMAND test_arms)

  # Heap allocations per call of each arm (a count, never a timing). Linux only: on Windows a
  # replaced operator new does not reach allocations made inside the C++ runtime's DLL.
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_executable(test_allocs test_allocs.cpp)
    target_include_directories(test_allocs PRIVATE ${PROJECT_SOURCE_DIR}/tests)
    target_link_libraries(test_allocs PRIVATE e2e_arms)
    tep_test_target(test_allocs)
    tep_counting_new(test_allocs)
    add_test(NAME allocs COMMAND test_allocs)
    set_tests_properties(allocs PROPERTIES SKIP_RETURN_CODE 77)
  endif()
endif()

# The transport and the benchmark server. libnghttp2 is the pinned static build under
# TEP_NGHTTP2_ROOT (bench/build_nghttp2.sh); without it, on Linux only, the system's (pkg-config).
if(TEP_NGHTTP2_ROOT)
  if(NOT EXISTS ${TEP_NGHTTP2_ROOT}/lib/cmake/nghttp2)
    message(FATAL_ERROR "TEP_NGHTTP2_ROOT (${TEP_NGHTTP2_ROOT}) has no lib/cmake/nghttp2; see bench/build_nghttp2.sh")
  endif()
  find_package(nghttp2 1.70.0 EXACT CONFIG REQUIRED PATHS ${TEP_NGHTTP2_ROOT} NO_DEFAULT_PATH)
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
# Only the ssize_t-free nghttp2 API is used; MSVC has no ssize_t for the deprecated one.
target_compile_definitions(e2e_transport PUBLIC NGHTTP2_NO_SSIZE_T)
tep_test_target(e2e_transport)

add_executable(tep_server main.cpp)
target_link_libraries(tep_server PRIVATE e2e_transport)
tep_test_target(tep_server)

add_executable(test_server test_server.cpp)
target_include_directories(test_server PRIVATE ${PROJECT_SOURCE_DIR}/tests)
target_link_libraries(test_server PRIVATE e2e_transport)
tep_test_target(test_server)
add_test(NAME server COMMAND test_server)

# Interoperability with grpc-go (design note section 6), when its programs are built. Asking for
# it where arm R cannot be built, or without the programs, stops the configuration, so a green
# test run never hides an interop test that was not registered.
if(TEP_INTEROP_BIN)
  if(NOT TEP_E2E_ARM_R)
    message(FATAL_ERROR "TEP_INTEROP_BIN needs arms R and P (P2996, not TEP_E2E_TRANSPORT_ONLY)")
  endif()
  get_filename_component(tep_interop_bin "${TEP_INTEROP_BIN}" ABSOLUTE)
  file(TO_CMAKE_PATH "${tep_interop_bin}" tep_interop_bin)
  file(TO_CMAKE_PATH "${TEP_GHZ}" tep_ghz)
  foreach(program ${tep_interop_bin}/interop_client${CMAKE_EXECUTABLE_SUFFIX}
                  ${tep_interop_bin}/interop_server${CMAKE_EXECUTABLE_SUFFIX} ${tep_ghz})
    if(NOT EXISTS "${program}")
      message(FATAL_ERROR "interop test: '${program}' does not exist (tools/build_interop.sh; TEP_GHZ)")
    endif()
  endforeach()
  add_executable(test_interop test_interop.cpp)
  target_include_directories(test_interop PRIVATE ${PROJECT_SOURCE_DIR}/tests)
  target_link_libraries(test_interop PRIVATE e2e_transport tep_reflect)
  target_compile_definitions(test_interop PRIVATE TEP_INTEROP_BIN="${tep_interop_bin}" TEP_GHZ="${tep_ghz}"
                                                  TEP_E2E_PROTO="${CMAKE_CURRENT_SOURCE_DIR}/e2e.proto")
  tep_test_target(test_interop)
  add_test(NAME interop COMMAND test_interop)
  set_tests_properties(interop PROPERTIES TIMEOUT 120)
endif()
