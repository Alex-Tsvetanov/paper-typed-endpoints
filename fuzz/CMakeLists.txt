# Fuzz targets (the brief: parsers get a libFuzzer target under ASan). Each target defines
# LLVMFuzzerTestOneInput. In every build it also links standalone_main.cpp into a CTest smoke test
# that runs the seeds and a fixed number of deterministic mutations under the preset's
# sanitizers. With clang and TEP_FUZZ=ON (preset linux-clang-fuzz) each portable target is also a
# libFuzzer executable. The decoder needs P2996, so it is GCC only, without libFuzzer.
set(TEP_FUZZ_MUTATIONS 20000 CACHE STRING "Mutations per fuzz smoke test")

file(GLOB TEP_SCHEMA_SEEDS ${PROJECT_SOURCE_DIR}/tests/golden/*.proto ${CMAKE_CURRENT_SOURCE_DIR}/seeds/*.proto)
file(GLOB TEP_WIRE_SEEDS ${PROJECT_SOURCE_DIR}/tests/golden/*.bin)

function(tep_fuzz name library)
  add_executable(${name}_smoke ${name}.cpp standalone_main.cpp)
  target_link_libraries(${name}_smoke PRIVATE ${library})
  tep_test_target(${name}_smoke)
  add_test(NAME ${name} COMMAND ${name}_smoke ${TEP_FUZZ_MUTATIONS} ${ARGN})
  if(TEP_FUZZ AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE ${library})
    tep_test_target(${name})
    target_compile_options(${name} PRIVATE -fsanitize=fuzzer)
    target_link_options(${name} PRIVATE -fsanitize=fuzzer)
  endif()
endfunction()

tep_fuzz(fuzz_schema tep ${TEP_SCHEMA_SEEDS})
tep_fuzz(fuzz_wire tep ${TEP_WIRE_SEEDS})
if(TEP_HAVE_REFLECTION)
  tep_fuzz(fuzz_decode tep_reflect ${PROJECT_SOURCE_DIR}/tests/golden/everything.bin)
endif()
