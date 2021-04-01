cmake_minimum_required(VERSION 3.14)

project(tokoeka
    VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)

add_library(tokoeka STATIC 
    src/solver.cpp
    src/index_ht.cpp
)
set_target_properties(tokoeka PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)
target_include_directories(tokoeka PUBLIC include)

get_directory_property(HAS_PARENT PARENT_DIRECTORY)
if (NOT HAS_PARENT)
    add_subdirectory(benchmark)

    enable_testing()
    add_subdirectory(tests)
endif()
