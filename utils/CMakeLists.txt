
cmake_minimum_required (VERSION 2.6)
project (stutools)

set( CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -Werror -Wall -pedantic --std=c99 -O2" )
#SET (CMAKE_C_COMPILER             "/usr/bin/clang")

add_executable(median median.c)

add_executable(dist dist.c)

add_executable(ramtester ramtester.c)

add_executable(raidfailures raidfailures.c)





