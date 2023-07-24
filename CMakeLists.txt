# cmake -S . -B build
# cd build
# cmake --build . 
# Pedro Vicente

if (MSVC)
 cmake_minimum_required(VERSION 3.26)
else()
 cmake_minimum_required(VERSION 3.15)
endif() 
set(CMAKE_BUILD_TYPE Debug)
project (nostrdb C)

#//////////////////////////
# sources 
#//////////////////////////

set(src ${src})
set(src ${src} nostrdb.c)
set(src ${src} nostrdb.h)
set(src ${src} cursor.h)
set(src ${src} hex.h)
set(src ${src} jsmn.h)

add_library(libnostrdb ${src})

#//////////////////////////
# link with libraries
# lib_dep contains a cascade definition of all the libraries needed to link
#//////////////////////////

set(lib_dep ${lib_dep})
set(lib_dep ${lib_dep} libnostrdb)

#//////////////////////////
# executables
#//////////////////////////

add_executable(test test.c)
add_executable(bench bench.c)
target_link_libraries (test ${lib_dep})
target_link_libraries (bench ${lib_dep})




