# ps2-multitap-pads: a poller for up to eight PlayStation 2 controllers through two multitaps (README.md).
#
#   Standalone:   cmake -S packages/ps2-multitap-pads -B build-mpad -G Ninja && cmake --build build-mpad && ctest --test-dir build-mpad
#   In a project: add_subdirectory(<path>/ps2-multitap-pads)   then   target_link_libraries(my-test PRIVATE mpad)
#                 or fetch it at a tag with FetchContent (examples/consumer/CMakeLists.txt).
#   On the console: the PS2SDK's own Makefile builds src/mpad.c and src/ps2.c (and src/ps2_bin2c.c); see README.md.
#
# The library `mpad` is the poller only (src/mpad.c): no console calls, builds anywhere. The PS2SDK glue is not part of
# it. With -DMPAD_PS2_GLUE=ON (inside a PS2SDK CMake toolchain) the library `mpad_ps2` is added.
#
# Variables it gives the including project:
#   MPAD_VERSION   the library's version
# Options: MPAD_ENABLE_TESTS (ctest entries when included in another project; always on standalone).
#          MPAD_PS2_GLUE     add the `mpad_ps2` library (needs the PS2SDK headers). Off by default.
cmake_minimum_required(VERSION 3.20)
if(NOT PROJECT_NAME)
  project(ps2_multitap_pads LANGUAGES C)
endif()

# the version, read from include/mpad/mpad.h so it is written once
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/include/mpad/mpad.h" MPAD_VERSION_LINE REGEX "^#define MPAD_VERSION ")
string(REGEX MATCH "\"([^\"]+)\"" MPAD_VERSION_MATCH "${MPAD_VERSION_LINE}")
set(MPAD_VERSION "${CMAKE_MATCH_1}")
set(MPAD_VERSION "${MPAD_VERSION}" CACHE INTERNAL "the ps2-multitap-pads version")
message(STATUS "ps2-multitap-pads ${MPAD_VERSION}")

option(MPAD_PS2_GLUE "build the PS2SDK glue library mpad_ps2 (needs the PS2SDK headers)" OFF)

add_library(mpad STATIC src/mpad.c)
add_library(mpad::mpad ALIAS mpad)
target_include_directories(mpad PUBLIC include)
set_target_properties(mpad PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF POSITION_INDEPENDENT_CODE ON)
if(NOT MSVC)
  target_compile_options(mpad PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(MPAD_PS2_GLUE)
  add_library(mpad_ps2 STATIC src/ps2.c)
  add_library(mpad::mpad_ps2 ALIAS mpad_ps2)
  target_link_libraries(mpad_ps2 PUBLIC mpad)
  set_target_properties(mpad_ps2 PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
endif()

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR OR MPAD_ENABLE_TESTS)
  enable_testing()

  # the poller against a fake pad driver
  add_executable(mpad-test tests/mpad_test.c)
  target_link_libraries(mpad-test PRIVATE mpad)
  set_target_properties(mpad-test PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
  if(NOT MSVC)
    target_compile_options(mpad-test PRIVATE -Wall -Wextra -Wpedantic)
  endif()
  add_test(NAME mpad-test COMMAND mpad-test)

  # the PS2SDK glue, built against stand-in headers and fake SDK functions: its mapping, not the real SDK
  add_executable(mpad-ps2-glue-test tests/ps2_glue_test.c src/ps2.c)
  target_include_directories(mpad-ps2-glue-test PRIVATE tests/ps2sdk-stub)
  target_link_libraries(mpad-ps2-glue-test PRIVATE mpad)
  set_target_properties(mpad-ps2-glue-test PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
  if(NOT MSVC)
    target_compile_options(mpad-ps2-glue-test PRIVATE -Wall -Wextra -Wpedantic)
  endif()
  add_test(NAME mpad-ps2-glue-test COMMAND mpad-ps2-glue-test)
endif()
