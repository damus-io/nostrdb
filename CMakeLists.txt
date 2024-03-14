# cmake -S . -B build
# cd build
# cmake --build . --target configurator
# cmake --build .
# Pedro Vicente

if (MSVC)
 cmake_minimum_required(VERSION 3.26)
else()
 cmake_minimum_required(VERSION 3.15)
endif()
set(CMAKE_BUILD_TYPE Debug)
project (nostrdb C)

set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
set(BUILD_SHARED_LIBS OFF)

include_directories(${CMAKE_SOURCE_DIR}/deps/secp256k1/include)
add_subdirectory(${CMAKE_SOURCE_DIR}/deps/secp256k1)

#//////////////////////////
# sources
#//////////////////////////

set(src ${src})
set(src ${src} nostrdb.c)
set(src ${src} nostrdb.h)
set(src ${src} cursor.h)
set(src ${src} hex.h)
set(src ${src} jsmn.h)
set(src ${src} sha256.c)
set(src ${src} sha256.h)
set(src ${src} random.h)

add_library(nostrdb ${src})

#//////////////////////////
# link with libraries
# lib_dep contains a cascade definition of all the libraries needed to link
#//////////////////////////

set(lib_dep ${lib_dep})
set(lib_dep ${lib_dep} nostrdb)
set(lib_dep ${lib_dep} secp256k1)

#//////////////////////////
# executables
#//////////////////////////

add_executable(test test.c)
add_executable(benchmark bench.c)

# tool to generate file 'config.h', generate from a shell with
# configurator > config.h
add_executable(configurator configurator.c)

target_link_libraries (test ${lib_dep})
target_link_libraries (benchmark ${lib_dep})

#//////////////////////////
# generate  config.h
#//////////////////////////

add_custom_command(
    TARGET configurator
    POST_BUILD
    COMMAND configurator > config.h
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_BINARY_DIR}/config.h" ${CMAKE_SOURCE_DIR}
    COMMENT "generating config.h"
)


