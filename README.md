# tokoeka
A data oriented [Cassowary](https://constraints.cs.washington.edu/cassowary/) constraint solving algorithm implementation in C-like C++.

## What's tokoeka?
This library was derived from [Amoeba](https://github.com/starwing/amoeba) and [kiwi](https://github.com/nucleic/kiwi) projects with another sparse table under the hood (dictionary of keys instead of list/dictionary of dictionaries).
The main idea is to get rid of lots of small allocations and provide a fast row iteration by maintaining the list of symbol rows.
Performance characteristics are highly dependant on the hash table implementation and its load factor (current version is based on linear probing with backward shift deletion).

## Warning
The library is still under development, here are some things that must be resolved:
* provide better hash table implementation (robin hood)
* fix manual rehashing/adding to the full hash table
* test unsuccessful add_constraint paths
* cache term data for faster add_row and add_term
* pass allocation page size into the solver creation function
* delete variables used in constraints?

## Features
* up to 64k variables (including internal objective, slack, error and dummy ones)
* 4 total allocation: variables buffer, constraint buffer, terms buffer for open addressing hash table and one for the solver struct.
* fast row and column iteration (2 intrusive lists within element's term data in the hash table)

## Setup
The project is configured for the usage with CMake, link tokoeka target as a library to include public directories and link with its static library as well. In the case the library is placed in project tree:

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

