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

# Xcode 2.x forgets to create the output directory before linking
# the individual architectures.
if(CMAKE_OSX_ARCHITECTURES AND XCODE
    AND NOT "${XCODE_VERSION}" MATCHES "^[^12]")
  add_custom_command(
    TARGET CMakeDebugServerLibTests
    PRE_BUILD COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CFG_INTDIR}"
    )
endif()

foreach(test ${CMakeDebugServerLib_TESTS})
  add_test(CMakeDebugServerLib.${test} CMakeDebugServerLibTests ${test} ${${test}_ARGS})
endforeach()
