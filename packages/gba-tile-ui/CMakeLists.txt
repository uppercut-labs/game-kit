# gba-tile-ui: a software tile map and OAM layer for a Game Boy Advance game (README.md).
#
#   Standalone:   cmake -S packages/gba-tile-ui -B build-gtu -G Ninja && cmake --build build-gtu && ctest --test-dir build-gtu
#   In a project: add_subdirectory(<path>/gba-tile-ui)   then   target_link_libraries(my-test PRIVATE gtu)
#                 or fetch it at a tag with FetchContent (examples/consumer/CMakeLists.txt).
#   On the console: devkitARM projects use their own Makefile: add src/gba_tile_ui.c and -I include (README.md).
#
# The library is the core only (no libgba, no devkitARM): it builds with any C compiler and is tested on the host.
# include/gba_tile_ui/gba_tile_ui_gba.h is the optional libgba glue; the tests compile it against a fake <gba.h>.
#
# Variables it gives the including project:
#   GTU_VERSION  the library's version
# Options: GTU_ENABLE_TESTS (ctest entries when included in another project; always on standalone).
cmake_minimum_required(VERSION 3.20)
if(NOT PROJECT_NAME)
  project(gba_tile_ui LANGUAGES C)
endif()

# the version, read from include/gba_tile_ui/gba_tile_ui.h so it is written once
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/include/gba_tile_ui/gba_tile_ui.h" GTU_VERSION_LINE REGEX "^#define GTU_VERSION ")
string(REGEX MATCH "\"([^\"]+)\"" GTU_VERSION_MATCH "${GTU_VERSION_LINE}")
set(GTU_VERSION "${CMAKE_MATCH_1}")
set(GTU_VERSION "${GTU_VERSION}" CACHE INTERNAL "the gba-tile-ui version")
message(STATUS "gba-tile-ui ${GTU_VERSION}")

add_library(gtu STATIC src/gba_tile_ui.c)
add_library(gtu::gtu ALIAS gtu)
target_include_directories(gtu PUBLIC include)
set_target_properties(gtu PROPERTIES C_STANDARD 99 POSITION_INDEPENDENT_CODE ON)
if(NOT MSVC)
  target_compile_options(gtu PRIVATE -Wall -Wextra -pedantic)
endif()

add_executable(gtu-test tests/gtu_test.c)
target_link_libraries(gtu-test PRIVATE gtu)
target_include_directories(gtu-test PRIVATE tests/fake_gba)
if(NOT MSVC)
  target_compile_options(gtu-test PRIVATE -Wall -Wextra -pedantic)
endif()
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR OR GTU_ENABLE_TESTS)
  enable_testing()
  add_test(NAME gtu-test COMMAND gtu-test)
endif()
