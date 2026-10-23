# Compile-fail check: the target must fail to build, and the build output must match REGEX.
# Usage: cmake -DBUILD_DIR=<dir> -DTARGET=<target> -DREGEX=<regex> -P expect_build_failure.cmake
execute_process(COMMAND ${CMAKE_COMMAND} --build ${BUILD_DIR} --target ${TARGET}
                RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(rc EQUAL 0)
  message(FATAL_ERROR "the build of ${TARGET} succeeded, but it must fail")
endif()
string(REGEX MATCH "${REGEX}" hit "${out}${err}")
if(NOT hit)
  message(FATAL_ERROR "the build of ${TARGET} failed without the expected diagnostic '${REGEX}':\n${out}${err}")
endif()
message(STATUS "failed as expected: ${hit}")
