cmake_minimum_required(VERSION 3.14)

Include(FetchContent)

#
# Setup Catch2
#
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v2.13.4)

FetchContent_MakeAvailable(Catch2)

include(${Catch2_SOURCE_DIR}/contrib/Catch.cmake)

#
# Tests
#

set(LIBS
    Catch2::Catch2WithMain 
    tokoeka
)

add_executable(test_cassowary test_cassowary.cpp)
target_link_libraries(test_cassowary PRIVATE ${LIBS})
catch_discover_tests(test_cassowary)

# add_executable(test_ht test_hash_table.cpp)
# target_link_libraries(test_ht PRIVATE ${LIBS})
# catch_discover_tests(test_ht)
