cmake_minimum_required(VERSION 3.4)

project(buildsystem2)

set(var1 123)

set(var2 345)

add_executable(main main.cpp)

add_executable(m_other main.cpp)

add_library(foo foo.cpp)

function(f1)
  set(var4 "${ARGN}")
  set(var5 "")
endfunction()

set(var3 345)

add_library(someImportedLib UNKNOWN IMPORTED)

add_subdirectory(subdir)

f1(${var1} ${var2})

f1()

set(var6 "Done!")
