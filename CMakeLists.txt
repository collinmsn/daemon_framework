
cmake_minimum_required (VERSION 2.6)
project (object_pool)
 
SET(CXX_FLAGS
 -g
 -rdynamic
 -fPIC
 -Wall
 -Wextra
 -Werror
 -Woverloaded-virtual
 -Wpointer-arith
 -Wwrite-strings
 -Wno-unused-parameter
 -Wno-reorder
 -Wno-unused-variable
 -DVALGRIND
 -D_FILE_OFFSET_BITS=64
 -DHAVE_STDINT_H
 -DHAVE_INTTYPES_H
 )

STRING(REPLACE ";" " " CMAKE_CXX_FLAGS "${CXX_FLAGS}")
enable_testing()
file(GLOB srcs "./*.cc")
add_executable(daemon_framework ${srcs})
target_link_libraries(daemon_framework glog gflags)
