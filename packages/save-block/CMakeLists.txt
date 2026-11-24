# save-block: a versioned, checksummed save record in two alternating slots, a byte codec and an optional arcade
# profile (README.md). C99, no platform headers: the game provides storage as two callbacks.
#
#   Standalone:   cmake -S packages/save-block -B build-save-block -G Ninja && cmake --build build-save-block && ctest --test-dir build-save-block
#   In a project: add_subdirectory(<path>/save-block)   then   target_link_libraries(my-game PRIVATE save_block::save_block)
#                 or fetch it at a tag with FetchContent (examples/consumer/CMakeLists.txt).
#
# It uses det-core for FNV-1a. That is found in this order: a det_core::det_core target the including project already
# made (for example from its own FetchContent of det-core at a tag), the sibling folder ../det-core (a checkout of
# game-kit, or a FetchContent clone of it, has one), then a FetchContent download of det-core@0.1.0 (a folder packed
# on its own, which needs git and network; set FETCHCONTENT_SOURCE_DIR_DET_CORE for an offline build).
#
# Variables it gives the including project:
#   SB_VERSION                the package version
#   SB_MAX_PAYLOAD_BYTES      the largest payload a format may have (cache variable, default 512)
# Options: SB_ENABLE_TESTS (ctest entries when included in another project; always on standalone).
cmake_minimum_required(VERSION 3.20)
if(NOT PROJECT_NAME)
  project(save-block LANGUAGES C)
endif()

# the version, read from include/sb/record.h so it is written once
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/include/sb/record.h" SB_VERSION_LINE REGEX "^#define SB_PACKAGE_VERSION ")
string(REGEX MATCH "\"([^\"]+)\"" SB_VERSION_MATCH "${SB_VERSION_LINE}")
set(SB_VERSION "${CMAKE_MATCH_1}")
set(SB_VERSION "${SB_VERSION}" CACHE INTERNAL "the save-block version")
message(STATUS "save-block ${SB_VERSION}")

set(SB_MAX_PAYLOAD_BYTES 512 CACHE STRING "the largest payload a save-block format may have, in bytes (sizes the stack buffers)")

if(NOT TARGET det_core::det_core)
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../det-core/CMakeLists.txt" AND NOT FETCHCONTENT_SOURCE_DIR_DET_CORE)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../det-core" "${CMAKE_CURRENT_BINARY_DIR}/det-core")
  else()
    include(FetchContent)
    FetchContent_Declare(det_core
      GIT_REPOSITORY https://github.com/uppercut-labs/game-kit.git
      GIT_TAG        det-core@0.1.0
      GIT_SHALLOW    TRUE
      SOURCE_SUBDIR  packages/det-core)
    FetchContent_MakeAvailable(det_core)
  endif()
endif()

add_library(save_block STATIC src/record.c src/memory.c src/arcade.c)
add_library(save_block::save_block ALIAS save_block)
target_include_directories(save_block PUBLIC include)
target_link_libraries(save_block PRIVATE det_core::det_core)
target_compile_definitions(save_block PUBLIC SB_MAX_PAYLOAD_BYTES=${SB_MAX_PAYLOAD_BYTES}u)
set_target_properties(save_block PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
if(MSVC)
  target_compile_options(save_block PRIVATE /W4)
else()
  target_compile_options(save_block PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR OR SB_ENABLE_TESTS)
  enable_testing()
  foreach(t codec record arcade)
    add_executable(sb-test-${t} tests/test_${t}.c)
    target_link_libraries(sb-test-${t} PRIVATE save_block)
    # the tests are the strictest consumer: C99 with no extensions, and every warning an error
    set_target_properties(sb-test-${t} PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
    if(MSVC)
      target_compile_options(sb-test-${t} PRIVATE /W4 /WX)
    else()
      target_compile_options(sb-test-${t} PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME sb-${t} COMMAND sb-test-${t})
  endforeach()
endif()
