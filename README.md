# tokoeka
A data oriented [Cassowary](https://constraints.cs.washington.edu/cassowary/) constraint solving algorithm C++ implementation.

## What's tokoeka?
This library was derived from [Amoeba](https://github.com/starwing/amoeba) and [kiwi](https://github.com/nucleic/kiwi) projects with another sparse table under the hood (dictionary of keys instead of list/dictionary of dictionaries).
The main idea is to get rid of lots of small allocations and provide fast row iteration by maintaining list of symbol rows.
Performance characteristics are highly dependant on hash the table implementation and its load factor (current is linear probing with backward shift deletion).

## Features
* up to 64k variables (including internal objective, slack, error and dummy ones)
* 4 total allocation: variables buffer, constraint buffer, terms buffer for open addressing hash table and one for the solver struct.
* fast row and column iteration (2 intrusive lists with term data in hash table)

## Setup
The project is configured for usage with CMake, link tokoeka target as library to include public directories and link with library as well. In the case the library is placed in project tree:
'''cmake
add_subdirectory(tokoeka)
add_executable(target_name ..)
target_link_libraries(target_name PRIVATE tokoeka)
'''
Or use [FetchContent](https://cmake.org/cmake/help/latest/module/FetchContent.html):
'''cmake
Include(FetchContent)

FetchContent_Declare(
  tokoeka
  GIT_REPOSITORY git@github.com:opengenius/tokoeka.git)

FetchContent_MakeAvailable(tokoeka)

add_executable(target_name ..)
target_link_libraries(tests PRIVATE tokoeka)
'''

