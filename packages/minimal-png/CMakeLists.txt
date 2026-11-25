# minimal-png: a PNG writer with no zlib (README.md).
#
#   Standalone:   cmake -S packages/minimal-png -B build-mpng -G Ninja && cmake --build build-mpng && ctest --test-dir build-mpng
#   In a project: add_subdirectory(<path>/minimal-png)   then   target_link_libraries(my-port PRIVATE mpng)
#                 or fetch it at a tag with FetchContent (examples/consumer/CMakeLists.txt).
#
# Variables it gives the including project:
#   MPNG_VERSION   the library's version
# Options: MPNG_ENABLE_TESTS (ctest entries when included in another project; always on standalone).
cmake_minimum_required(VERSION 3.20)
if(NOT PROJECT_NAME)
  project(minimal_png LANGUAGES C)
endif()

# the version, read from include/mpng/mpng.h so it is written once
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/include/mpng/mpng.h" MPNG_VERSION_LINE REGEX "^#define MPNG_VERSION ")
string(REGEX MATCH "\"([^\"]+)\"" MPNG_VERSION_MATCH "${MPNG_VERSION_LINE}")
set(MPNG_VERSION "${CMAKE_MATCH_1}")
set(MPNG_VERSION "${MPNG_VERSION}" CACHE INTERNAL "the minimal-png version")
message(STATUS "minimal-png ${MPNG_VERSION}")

add_library(mpng STATIC src/mpng.c)
add_library(mpng::mpng ALIAS mpng)
target_include_directories(mpng PUBLIC include)
set_target_properties(mpng PROPERTIES C_STANDARD 11 POSITION_INDEPENDENT_CODE ON)
if(NOT MSVC)
  target_compile_options(mpng PRIVATE -Wall -Wextra)
endif()

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR OR MPNG_ENABLE_TESTS)
  enable_testing()
  add_executable(mpng-test tests/mpng_test.c)
  target_link_libraries(mpng-test PRIVATE mpng)
  if(NOT MSVC)
    target_compile_options(mpng-test PRIVATE -Wall -Wextra)
  endif()
  add_test(NAME mpng-test COMMAND mpng-test "${CMAKE_CURRENT_BINARY_DIR}")

  # With Python 3 present, a second test has zlib's own reader decode what the C code wrote.
  find_package(Python3 COMPONENTS Interpreter QUIET)
  if(Python3_Interpreter_FOUND)
    add_executable(mpng-write tests/mpng_write.c)
    target_link_libraries(mpng-write PRIVATE mpng)
    add_test(NAME mpng-zlib-read
      COMMAND "${Python3_EXECUTABLE}" -I "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_with_zlib.py"
              "$<TARGET_FILE:mpng-write>" "${CMAKE_CURRENT_BINARY_DIR}")
  endif()
endif()
