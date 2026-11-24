# bitmap-font: small bitmap fonts as tables, with a glyph-drawing callback (README.md).
#
#   Standalone:   cmake -S packages/bitmap-font -B build-bfont -G Ninja && cmake --build build-bfont && ctest --test-dir build-bfont
#   In a project: add_subdirectory(<path>/bitmap-font)   then   target_link_libraries(my-game PRIVATE bfont)
#                 or fetch it at a tag with FetchContent (examples/consumer/CMakeLists.txt).
#
# Variables it gives the including project:
#   BFONT_VERSION   the library's version
# Options: BFONT_ENABLE_TESTS (ctest entries when included in another project; always on standalone).
cmake_minimum_required(VERSION 3.20)
if(NOT PROJECT_NAME)
  project(bitmap_font LANGUAGES C)
endif()

# the version, read from include/bfont/bfont.h so it is written once
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/include/bfont/bfont.h" BFONT_VERSION_LINE REGEX "^#define BFONT_VERSION ")
string(REGEX MATCH "\"([^\"]+)\"" BFONT_VERSION_MATCH "${BFONT_VERSION_LINE}")
set(BFONT_VERSION "${CMAKE_MATCH_1}")
set(BFONT_VERSION "${BFONT_VERSION}" CACHE INTERNAL "the bitmap-font version")
message(STATUS "bitmap-font ${BFONT_VERSION}")

add_library(bfont STATIC src/bfont.c)
add_library(bfont::bfont ALIAS bfont)
target_include_directories(bfont PUBLIC include)
set_target_properties(bfont PROPERTIES C_STANDARD 99 POSITION_INDEPENDENT_CODE ON)
if(NOT MSVC)
  target_compile_options(bfont PRIVATE -Wall -Wextra)
endif()

add_executable(bfont-test tests/bfont_test.c)
target_link_libraries(bfont-test PRIVATE bfont)
set_target_properties(bfont-test PROPERTIES C_STANDARD 99)
if(NOT MSVC)
  target_compile_options(bfont-test PRIVATE -Wall -Wextra)
endif()
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR OR BFONT_ENABLE_TESTS)
  enable_testing()
  add_test(NAME bfont-test COMMAND bfont-test)
  # the generator is plain Python 3: its own tests, and a check that the committed header is what it would write now
  find_package(Python3 COMPONENTS Interpreter QUIET)
  if(Python3_FOUND)
    add_test(NAME bfont-generator-test COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_generator.py)
    add_test(NAME bfont-generated-current
             COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_bitmap_font.py check)
  else()
    message(STATUS "bitmap-font: Python 3 not found, the generator tests are skipped")
  endif()
endif()
