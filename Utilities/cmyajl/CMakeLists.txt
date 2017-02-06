# Copyright (c) 2007-2014, Lloyd Hilaiel <me@lloyd.io>
#
# Permission to use, copy, modify, and/or distribute this software for any
# purpose with or without fee is hereby granted, provided that the above
# copyright notice and this permission notice appear in all copies.
#
# THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
# WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
# MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
# ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
# WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
# ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
# OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

CMAKE_MINIMUM_REQUIRED(VERSION 2.6)

PROJECT(YetAnotherJSONParser C)

# Disable warnings to avoid changing 3rd party code.
if(CMAKE_CXX_COMPILER_ID MATCHES
        "^(GNU|Clang|AppleClang|XL|VisualAge|SunPro|MIPSpro|HP|Intel)$")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -w")
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "PathScale")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -woffall")
endif()

set(YAJL_SOURCES
        src/yajl_alloc.c
        src/yajl_buf.c
        src/yajl.c
        src/yajl_encode.c
        src/yajl_gen.c
        src/yajl_lex.c
        src/yajl_parser.c
        src/yajl_tree.c
        src/yajl_version.c
        )

include_directories(
        include
)

add_library(cmyajl ${YAJL_SOURCES})
