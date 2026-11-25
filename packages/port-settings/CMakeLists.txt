# port-settings: a plain key=value settings file for a game port (README.md).
#
#   Standalone:   cmake -S packages/port-settings -B build-pset -G Ninja && cmake --build build-pset && ctest --test-dir build-pset
#   In a project: add_subdirectory(<path>/port-settings)   then   target_link_libraries(my-port PRIVATE pset)
#                 or fetch it at a tag with FetchContent (examples/consumer/CMakeLists.txt).
#
# Variables it gives the including project:
#   PSET_VERSION   the library's version
# Options: PSET_ENABLE_TESTS (ctest entries when included in another project; always on standalone).
cmake_minimum_required(VERSION 3.20)
if(NOT PROJECT_NAME)
  project(port_settings LANGUAGES C)
endif()

# the version, read from include/pset/pset.h so it is written once
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/include/pset/pset.h" PSET_VERSION_LINE REGEX "^#define PSET_VERSION ")
string(REGEX MATCH "\"([^\"]+)\"" PSET_VERSION_MATCH "${PSET_VERSION_LINE}")
set(PSET_VERSION "${CMAKE_MATCH_1}")
set(PSET_VERSION "${PSET_VERSION}" CACHE INTERNAL "the port-settings version")
message(STATUS "port-settings ${PSET_VERSION}")

add_library(pset STATIC src/pset.c)
add_library(pset::pset ALIAS pset)
target_include_directories(pset PUBLIC include)
set_target_properties(pset PROPERTIES C_STANDARD 11 POSITION_INDEPENDENT_CODE ON)
if(NOT MSVC)
  target_compile_options(pset PRIVATE -Wall -Wextra)
endif()

add_executable(pset-test tests/pset_test.c)
target_link_libraries(pset-test PRIVATE pset)
if(NOT MSVC)
  target_compile_options(pset-test PRIVATE -Wall -Wextra)
endif()
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR OR PSET_ENABLE_TESTS)
  enable_testing()
  add_test(NAME pset-test COMMAND pset-test "${CMAKE_CURRENT_BINARY_DIR}")
endif()
