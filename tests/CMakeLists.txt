find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(TEP_GOLDEN_DIR ${CMAKE_CURRENT_SOURCE_DIR}/golden)

# MinGW: the compiler's runtime DLLs are copied next to each executable, which the DLL search order
# prefers to PATH (where another MinGW runtime, such as Git for Windows', may come first).
function(tep_mingw_runtime target)
  get_target_property(type ${target} TYPE)
  if(MINGW AND type STREQUAL "EXECUTABLE")
    get_filename_component(bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
    foreach(dll libstdc++-6.dll libgcc_s_seh-1.dll libwinpthread-1.dll)
      add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${bin}/${dll}" "$<TARGET_FILE_DIR:${target}>")
    endforeach()
  endif()
endfunction()

function(tep_test_target target)
  target_compile_definitions(${target} PRIVATE TEP_GOLDEN_DIR="${TEP_GOLDEN_DIR}")
  tep_mingw_runtime(${target})
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

# A test that counts allocations through a replaced operator new: GCC 15 and later may otherwise
# assume operator new has no visible side effects and fold the counter's reads.
include(CheckCXXCompilerFlag)
check_cxx_compiler_flag(-fno-assume-sane-operators-new-delete TEP_HAVE_NO_SANE_NEW)
function(tep_counting_new target)
  if(TEP_HAVE_NO_SANE_NEW)
    target_compile_options(${target} PRIVATE -fno-assume-sane-operators-new-delete)
  endif()
endfunction()

foreach(name schema wire grpc)
  add_executable(test_${name} test_${name}.cpp)
  target_link_libraries(test_${name} PRIVATE tep)
  tep_test_target(test_${name})
  add_test(NAME ${name} COMMAND test_${name})
endforeach()

if(TEP_HAVE_REFLECTION)
  foreach(name reflect endpoint growth)
    add_executable(test_${name} test_${name}.cpp)
    target_link_libraries(test_${name} PRIVATE tep_reflect)
    tep_test_target(test_${name})
    add_test(NAME ${name} COMMAND test_${name})
  endforeach()
  tep_counting_new(test_growth)

  # Compile-fail checks: each target must fail to build with the expected diagnostic.
  function(tep_negative name library regex)
    add_executable(neg_${name} EXCLUDE_FROM_ALL negative/${name}.cpp)
    target_link_libraries(neg_${name} PRIVATE ${library})
    add_test(NAME negative_${name}
             COMMAND ${CMAKE_COMMAND} -DBUILD_DIR=${CMAKE_BINARY_DIR} -DTARGET=neg_${name} "-DREGEX=${regex}"
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/expect_build_failure.cmake)
    set_tests_properties(negative_${name} PROPERTIES RESOURCE_LOCK build_tree)
  endfunction()
  # Regexes use '.' for literal punctuation: backslash escapes do not survive into CTestTestfile.
  # Each check needs a failed build and a matching diagnostic (expect_build_failure.cmake).
  tep_negative(bad_schema tep_reflect "tep: schema error at line 4, column 13: duplicate field number ..1..")
  tep_negative(duplicate_definition tep_reflect "tep: schema error at line 3, column 9: name is defined twice ..DuplicatedMessage..")
  tep_negative(keyword_collision tep_reflect "tep: schema error at line 4, column 9: two fields have the same JSON name ..class..")
  tep_negative(streaming tep_reflect "tep: schema error at line 3, column 19: streaming rpcs are not supported")
  tep_negative(missing_handler tep_reflect "has no member function named 'Pong' for rpc p.S.Pong")
  tep_negative(wrong_signature tep_reflect "handler member function 'Ping' must be 'Response Ping.const Request&.'")
  tep_negative(private_handler tep_reflect "handler member function 'Pong' for rpc p.S.Pong is not public")
  tep_negative(overloaded_handler tep_reflect "handler member function 'Ping' is overloaded")
  tep_negative(no_reflection tep "needs C..26 static reflection .P2996.")
else()
  # Not silent: CTest lists this test as skipped, with the reason.
  add_test(NAME reflection_layer COMMAND ${Python3_EXECUTABLE} -c
           "import sys; print('SKIPPED: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION} has no C++26 reflection (P2996); the reflection layer and its tests are not built on this compiler'); sys.exit(77)")
  set_tests_properties(reflection_layer PROPERTIES SKIP_RETURN_CODE 77)
endif()

add_test(NAME name_guard COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/check_names.py ${PROJECT_SOURCE_DIR})
add_test(NAME name_guard_canary COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/check_names.py --self-test)
