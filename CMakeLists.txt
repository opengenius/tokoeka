cmake_minimum_required(VERSION 3.14)

project(tokoeka
    VERSION 0.1.0)

if (CMAKE_BUILD_TYPE EQUAL "Release")
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

add_library(tokoeka STATIC 
    src/solver.cpp)
target_include_directories(tokoeka PUBLIC include)

add_subdirectory(benchmark)

enable_testing()
add_subdirectory(tests)
