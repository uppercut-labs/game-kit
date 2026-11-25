# pad-replay: a plain-text controller replay, a strict reader and a recorder for a game port (README.md).
#
#   Standalone:   cmake -S packages/pad-replay -B build-padr -G Ninja && cmake --build build-padr && ctest --test-dir build-padr
#   In a project: add_subdirectory(<path>/pad-replay)   then   target_link_libraries(my-port PRIVATE padr)
#                 or fetch it at a tag with FetchContent (examples/consumer/CMakeLists.txt).
#
# Variables it gives the including project:
#   PADR_VERSION   the library's version
# Options: PADR_ENABLE_TESTS (ctest entries when included in another project; always on standalone).
cmake_minimum_required(VERSION 3.20)
if(NOT PROJECT_NAME)
  project(pad_replay LANGUAGES C)
endif()

# the version, read from include/padr/padr.h so it is written once
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/include/padr/padr.h" PADR_VERSION_LINE REGEX "^#define PADR_VERSION ")
string(REGEX MATCH "\"([^\"]+)\"" PADR_VERSION_MATCH "${PADR_VERSION_LINE}")
set(PADR_VERSION "${CMAKE_MATCH_1}")
set(PADR_VERSION "${PADR_VERSION}" CACHE INTERNAL "the pad-replay version")
message(STATUS "pad-replay ${PADR_VERSION}")

add_library(padr STATIC src/padr.c src/padtrace.c)
add_library(padr::padr ALIAS padr)
target_include_directories(padr PUBLIC include)
set_target_properties(padr PROPERTIES C_STANDARD 11 POSITION_INDEPENDENT_CODE ON)
if(NOT MSVC)
  target_compile_options(padr PRIVATE -Wall -Wextra)
endif()

add_executable(padr-test tests/padr_test.c)
target_link_libraries(padr-test PRIVATE padr)
if(NOT MSVC)
  target_compile_options(padr-test PRIVATE -Wall -Wextra)
endif()
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR OR PADR_ENABLE_TESTS)
  enable_testing()
  add_test(NAME padr-test COMMAND padr-test "${CMAKE_CURRENT_BINARY_DIR}")
endif()
