# kos-naomi-cabinet: a game's settings block in a NAOMI cabinet's EEPROM, coin credits and test/service switch edges,
# with KallistiOS glue (README.md). C99, no platform headers in the library.
#
#   Standalone:   cmake -S packages/kos-naomi-cabinet -B build-kcab -G Ninja && cmake --build build-kcab && ctest --test-dir build-kcab
#   In a project: add_subdirectory(<path>/kos-naomi-cabinet)   then   target_link_libraries(my-game PRIVATE kcab)
#                 or fetch it at a tag with FetchContent (examples/consumer/CMakeLists.txt).
#   On the console: the KOS Makefile builds src/kos.c (and src/kos_naomi_mode.c); see README.md.
#
# The library `kcab` is src/eeprom.c, src/ram.c and src/cabinet.c: it builds anywhere. The save-block adapter is the
# separate library `kcab_save_block` (src/save_block.c), added when save-block is available: a `save_block::save_block`
# target the including project already made, else the folder ../save-block next to this one (a checkout of game-kit
# has it), else not at all. Ask for it with -DKCAB_SAVE_BLOCK=ON (then a missing save-block is an error: the project
# must declare it first, or set FETCHCONTENT_SOURCE_DIR_SAVE_BLOCK to a game-kit checkout) or leave it out with OFF.
# The KallistiOS glue is not part of the library; with -DKCAB_KOS_GLUE=ON (inside a KOS toolchain) the library
# `kcab_kos` is added.
#
# Variables it gives the including project:
#   KCAB_VERSION   the library's version
# Options: KCAB_ENABLE_TESTS (ctest entries when included in another project; always on standalone).
#          KCAB_SAVE_BLOCK   AUTO (default), ON or OFF.
#          KCAB_KOS_GLUE     add the `kcab_kos` library (needs KallistiOS). Off by default.
cmake_minimum_required(VERSION 3.20)
if(NOT PROJECT_NAME)
  project(kos_naomi_cabinet LANGUAGES C)
endif()

# the version, read from include/kcab/eeprom.h so it is written once
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/include/kcab/eeprom.h" KCAB_VERSION_LINE REGEX "^#define KCAB_VERSION ")
string(REGEX MATCH "\"([^\"]+)\"" KCAB_VERSION_MATCH "${KCAB_VERSION_LINE}")
set(KCAB_VERSION "${CMAKE_MATCH_1}")
set(KCAB_VERSION "${KCAB_VERSION}" CACHE INTERNAL "the kos-naomi-cabinet version")
message(STATUS "kos-naomi-cabinet ${KCAB_VERSION}")

set(KCAB_SAVE_BLOCK AUTO CACHE STRING "build the save-block adapter: AUTO, ON or OFF")
set_property(CACHE KCAB_SAVE_BLOCK PROPERTY STRINGS AUTO ON OFF)
option(KCAB_KOS_GLUE "build the KallistiOS glue library kcab_kos (needs KallistiOS)" OFF)

function(kcab_c99 target)
  set_target_properties(${target} PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
  endif()
endfunction()

add_library(kcab STATIC src/eeprom.c src/ram.c src/cabinet.c)
add_library(kcab::kcab ALIAS kcab)
target_include_directories(kcab PUBLIC include)
kcab_c99(kcab)
set_target_properties(kcab PROPERTIES POSITION_INDEPENDENT_CODE ON)

# the save-block adapter
set(KCAB_HAVE_SAVE_BLOCK OFF)
if(NOT KCAB_SAVE_BLOCK STREQUAL "OFF")
  if(NOT TARGET save_block::save_block)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../save-block/CMakeLists.txt" AND NOT FETCHCONTENT_SOURCE_DIR_SAVE_BLOCK)
      add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../save-block" "${CMAKE_CURRENT_BINARY_DIR}/save-block")
    elseif(KCAB_SAVE_BLOCK STREQUAL "ON" OR FETCHCONTENT_SOURCE_DIR_SAVE_BLOCK)
      include(FetchContent)
      FetchContent_Declare(save_block
        GIT_REPOSITORY https://github.com/uppercut-labs/game-kit.git
        GIT_TAG        save-block@1.0.0
        GIT_SHALLOW    TRUE
        SOURCE_SUBDIR  packages/save-block)
      FetchContent_MakeAvailable(save_block)
    endif()
  endif()
  if(TARGET save_block::save_block)
    set(KCAB_HAVE_SAVE_BLOCK ON)
  elseif(KCAB_SAVE_BLOCK STREQUAL "ON")
    message(FATAL_ERROR "KCAB_SAVE_BLOCK=ON but no save_block::save_block target and no ../save-block folder")
  else()
    message(STATUS "kos-naomi-cabinet: save-block not found, the save-block adapter is not built")
  endif()
endif()
if(KCAB_HAVE_SAVE_BLOCK)
  add_library(kcab_save_block STATIC src/save_block.c)
  add_library(kcab::save_block ALIAS kcab_save_block)
  target_link_libraries(kcab_save_block PUBLIC kcab save_block::save_block)
  kcab_c99(kcab_save_block)
endif()

if(KCAB_KOS_GLUE)
  add_library(kcab_kos STATIC src/kos.c)
  add_library(kcab::kos ALIAS kcab_kos)
  target_link_libraries(kcab_kos PUBLIC kcab)
  set_target_properties(kcab_kos PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON)
endif()

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR OR KCAB_ENABLE_TESTS)
  enable_testing()
  function(kcab_test name)
    add_executable(${name} ${ARGN})
    kcab_c99(${name})
    if(NOT MSVC)
      target_compile_options(${name} PRIVATE -Werror)
    else()
      target_compile_options(${name} PRIVATE /WX)
    endif()
    add_test(NAME ${name} COMMAND ${name})
  endfunction()

  kcab_test(kcab-test-eeprom tests/test_eeprom.c)
  target_link_libraries(kcab-test-eeprom PRIVATE kcab)
  kcab_test(kcab-test-cabinet tests/test_cabinet.c)
  target_link_libraries(kcab-test-cabinet PRIVATE kcab)

  # the KallistiOS glue against stand-in headers (tests/kos-stub) and fake driver functions: its mapping, not KOS
  kcab_test(kcab-test-kos-glue tests/test_kos_glue.c src/kos.c src/kos_naomi_mode.c)
  target_include_directories(kcab-test-kos-glue PRIVATE tests/kos-stub)
  set_target_properties(kcab-test-kos-glue PROPERTIES C_STANDARD 99 C_EXTENSIONS OFF)
  target_link_libraries(kcab-test-kos-glue PRIVATE kcab)

  if(KCAB_HAVE_SAVE_BLOCK)
    kcab_test(kcab-test-save-block tests/test_save_block.c)
    target_link_libraries(kcab-test-save-block PRIVATE kcab_save_block)
  endif()
endif()
