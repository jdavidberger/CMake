cmake_minimum_required(VERSION 3.4)
project(Server CXX)

find_package(PythonInterp REQUIRED)

macro(do_test bsname file type)
  execute_process(COMMAND ${PYTHON_EXECUTABLE}
    -B # no .pyc files
    "${CMAKE_SOURCE_DIR}/${type}-test.py"
    "${CMAKE_COMMAND}"
    "${CMAKE_SOURCE_DIR}/${file}"
    "${CMAKE_SOURCE_DIR}"
    "${CMAKE_BINARY_DIR}"
    "${CMAKE_GENERATOR}"
    RESULT_VARIABLE test_result
    )

  if (NOT test_result EQUAL 0)
    message(FATAL_ERROR "TEST FAILED")
  endif()
endmacro()

do_test("test_handshake" "tc_handshake.json" "server")
do_test("test_globalSettings" "tc_globalSettings.json" "server")
do_test("test_buildsystem1" "tc_buildsystem1.json" "server")

do_test("test_connects" "tc_connects.json" "debugger")
do_test("test_breakpoint" "tc_breakpoints.json" "debugger")
do_test("test_watchpoints" "tc_watchpoints.json" "debugger")
do_test("test_psycho" "tc_psycho.json" "debugger")


add_executable(Server empty.cpp)
