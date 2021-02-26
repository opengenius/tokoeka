cmake_minimum_required(VERSION 3.14)
set(CMAKE_CXX_STANDARD 17)

Include(FetchContent)

#
# Setup nanobench
#
FetchContent_Declare(
    nanobench
    GIT_REPOSITORY https://github.com/martinus/nanobench.git
    GIT_TAG v4.3.1
    GIT_SHALLOW TRUE)

FetchContent_MakeAvailable(nanobench)

#
# Benchmark executable
#

add_executable(enaml_benchmark enaml_like_benchmark.cpp)
target_link_libraries(enaml_benchmark 
                        PRIVATE nanobench tokoeka)
