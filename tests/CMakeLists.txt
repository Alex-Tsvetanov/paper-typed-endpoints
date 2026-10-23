find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(TEP_GOLDEN_DIR ${CMAKE_CURRENT_SOURCE_DIR}/golden)

function(tep_test_target target)
  target_compile_definitions(${target} PRIVATE TEP_GOLDEN_DIR="${TEP_GOLDEN_DIR}")
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive- /Zc:preprocessor /Zc:__cplusplus /utf-8 /EHsc
                                             /constexpr:steps10000000)
    if(TEP_WERROR)
      target_compile_options(${target} PRIVATE /WX)
    endif()
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow)
    if(TEP_WERROR)
      target_compile_options(${target} PRIVATE -Werror)
    endif()
  endif()
endfunction()

foreach(name schema wire grpc)
  add_executable(test_${name} test_${name}.cpp)
  target_link_libraries(test_${name} PRIVATE tep)
  tep_test_target(test_${name})
  add_test(NAME ${name} COMMAND test_${name})
endforeach()

if(TEP_HAVE_REFLECTION)
  foreach(name reflect endpoint)
    add_executable(test_${name} test_${name}.cpp)
    target_link_libraries(test_${name} PRIVATE tep_reflect)
    tep_test_target(test_${name})
    add_test(NAME ${name} COMMAND test_${name})
  endforeach()

  # Compile-fail checks: each target must fail to build with the expected diagnostic.
  function(tep_negative name library regex)
    add_executable(neg_${name} EXCLUDE_FROM_ALL negative/${name}.cpp)
    target_link_libraries(neg_${name} PRIVATE ${library})
    add_test(NAME negative_${name} COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target neg_${name})
    set_tests_properties(negative_${name} PROPERTIES PASS_REGULAR_EXPRESSION "${regex}" RESOURCE_LOCK build_tree)
  endfunction()
  # Regexes use '.' for literal punctuation: backslash escapes do not survive into CTestTestfile.
  tep_negative(bad_schema tep_reflect "tep: schema error at line 4, column 9: duplicate field number ..b..")
  tep_negative(streaming tep_reflect "tep: schema error at line 3, column 19: streaming rpcs are not supported")
  tep_negative(missing_handler tep_reflect "has no member function named 'Pong' for rpc p.S.Pong")
  tep_negative(wrong_signature tep_reflect "handler member function 'Ping' must be 'Response Ping.const Request&.'")
  tep_negative(no_reflection tep "needs C..26 static reflection .P2996.")
else()
  # Not silent: CTest lists this test as skipped, with the reason.
  add_test(NAME reflection_layer COMMAND ${Python3_EXECUTABLE} -c
           "import sys; print('SKIPPED: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION} has no C++26 reflection (P2996); the reflection layer and its tests are not built on this compiler'); sys.exit(77)")
  set_tests_properties(reflection_layer PROPERTIES SKIP_RETURN_CODE 77)
endif()

add_test(NAME name_guard COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/check_names.py ${PROJECT_SOURCE_DIR})
add_test(NAME name_guard_canary COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/check_names.py --self-test)
