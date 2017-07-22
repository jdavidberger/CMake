include_directories(
  ${CMAKE_CURRENT_BINARY_DIR}
  ${CMake_BINARY_DIR}/Source
  ${CMake_SOURCE_DIR}/Source
  )

set(CMakeDebugServerLib_TESTS
  testServerBuffering
  )

create_test_sourcelist(CMakeLib_TEST_SRCS CMakeDebugServerLibTests.cxx ${CMakeDebugServerLib_TESTS})
add_executable(CMakeDebugServerLibTests ${CMakeLib_TEST_SRCS})
target_link_libraries(CMakeDebugServerLibTests CMakeLib CMakeDebugServerLib)

foreach(test ${CMakeDebugServerLib_TESTS})
  add_test(CMakeDebugServerLib.${test} CMakeDebugServerLibTests ${test} ${${test}_ARGS})
endforeach()
