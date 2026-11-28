# ps2-audsrv-stream: keeps the PlayStation 2's audsrv ring fed once a frame from a PCM callback (README.md).
#
#   Standalone:   cmake -S packages/ps2-audsrv-stream -B build-pas -G Ninja && cmake --build build-pas && ctest --test-dir build-pas
#   In a project: add_subdirectory(<path>/ps2-audsrv-stream)   then   target_link_libraries(my-test PRIVATE pas)
#                 or fetch it at a tag with FetchContent (examples/consumer/CMakeLists.txt).
#   On the console: the PS2SDK's own Makefile builds src/pas.c and src/ps2.c (and src/ps2_bin2c.c); see README.md.
#
# The library `pas` is the stream only (src/pas.c): no console calls, builds anywhere. The PS2SDK glue is not part of
# it. With -DPAS_PS2_GLUE=ON (inside a PS2SDK CMake toolchain) the library `pas_ps2` is added.
#
# Variables it gives the including project:
#   PAS_VERSION   the library's version
# Options: PAS_ENABLE_TESTS (ctest entries when included in another project; always on standalone).
#          PAS_PS2_GLUE     add the `pas_ps2` library (needs the PS2SDK headers). Off by default.
cmake_minimum_required(VERSION 3.20)
if(NOT PROJECT_NAME)
  project(ps2_audsrv_stream LANGUAGES C)
endif()

# the version, read from include/pas/pas.h so it is written once
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/include/pas/pas.h" PAS_VERSION_LINE REGEX "^#define PAS_VERSION ")
string(REGEX MATCH "\"([^\"]+)\"" PAS_VERSION_MATCH "${PAS_VERSION_LINE}")
set(PAS_VERSION "${CMAKE_MATCH_1}")
set(PAS_VERSION "${PAS_VERSION}" CACHE INTERNAL "the ps2-audsrv-stream version")
message(STATUS "ps2-audsrv-stream ${PAS_VERSION}")

option(PAS_PS2_GLUE "build the PS2SDK glue library pas_ps2 (needs the PS2SDK headers)" OFF)

add_library(pas STATIC src/pas.c)
add_library(pas::pas ALIAS pas)
target_include_directories(pas PUBLIC include)
set_target_properties(pas PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF POSITION_INDEPENDENT_CODE ON)
if(NOT MSVC)
  target_compile_options(pas PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(PAS_PS2_GLUE)
  add_library(pas_ps2 STATIC src/ps2.c src/ps2_bin2c.c)
  add_library(pas::pas_ps2 ALIAS pas_ps2)
  target_link_libraries(pas_ps2 PUBLIC pas)
  set_target_properties(pas_ps2 PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
endif()

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR OR PAS_ENABLE_TESTS)
  enable_testing()

  # the stream against a fake ring
  add_executable(pas-test tests/pas_test.c)
  target_link_libraries(pas-test PRIVATE pas)
  set_target_properties(pas-test PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
  if(NOT MSVC)
    target_compile_options(pas-test PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()
  add_test(NAME pas-test COMMAND pas-test)

  # the PS2SDK glue, built against stand-in headers and fake SDK functions: its calls, not the real SDK
  add_executable(pas-ps2-glue-test tests/ps2_glue_test.c src/ps2.c src/ps2_bin2c.c)
  target_include_directories(pas-ps2-glue-test PRIVATE tests/ps2sdk-stub)
  target_link_libraries(pas-ps2-glue-test PRIVATE pas)
  set_target_properties(pas-ps2-glue-test PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
  if(NOT MSVC)
    target_compile_options(pas-ps2-glue-test PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()
  add_test(NAME pas-ps2-glue-test COMMAND pas-ps2-glue-test)
endif()
