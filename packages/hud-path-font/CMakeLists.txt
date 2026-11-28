# hud-path-font: single-stroke HUD glyph paths, rasterized once into a 4-bit coverage atlas (README.md).
#
#   Standalone:   cmake -S packages/hud-path-font -B build-hud-path-font -G Ninja && cmake --build build-hud-path-font && ctest --test-dir build-hud-path-font
#   In a project: add_subdirectory(<path>/hud-path-font)   then   target_link_libraries(my-game PRIVATE hud_path_font)
#                 or fetch it at a tag with FetchContent (examples/consumer/CMakeLists.txt).
#   On a console: add src/hud_path_font.c to the build and include/ to the include path; see README.md.
#
# Variables it gives the including project:
#   HUD_PATH_FONT_VERSION   the library's version
# Options: HUD_PATH_FONT_ENABLE_TESTS  (ctest entries when included in another project; always on standalone).
#          HUD_PATH_FONT_PARITY_DIRS   a list of folders, each holding an earlier copy of the font (hud_font.c, hud_font.h
#                                      and the headers they include); each gets a ctest entry that checks it builds the
#                                      same atlas as the package. Empty by default.
cmake_minimum_required(VERSION 3.20)
if(NOT PROJECT_NAME)
  project(hud_path_font LANGUAGES C)
endif()

# the version, read from include/hud_path_font/hud_path_font.h so it is written once
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/include/hud_path_font/hud_path_font.h" HUD_PATH_FONT_VERSION_LINE
     REGEX "^#define HUD_PATH_FONT_VERSION ")
string(REGEX MATCH "\"([^\"]+)\"" HUD_PATH_FONT_VERSION_MATCH "${HUD_PATH_FONT_VERSION_LINE}")
set(HUD_PATH_FONT_VERSION "${CMAKE_MATCH_1}")
set(HUD_PATH_FONT_VERSION "${HUD_PATH_FONT_VERSION}" CACHE INTERNAL "the hud-path-font version")
message(STATUS "hud-path-font ${HUD_PATH_FONT_VERSION}")

set(HUD_PATH_FONT_PARITY_DIRS "" CACHE STRING "folders holding an earlier copy of the font (hud_font.c) to check")

add_library(hud_path_font STATIC src/hud_path_font.c)
add_library(hud_path_font::hud_path_font ALIAS hud_path_font)
target_include_directories(hud_path_font PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
set_target_properties(hud_path_font PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON POSITION_INDEPENDENT_CODE ON)
if(MSVC)
  target_compile_options(hud_path_font PRIVATE /W4)
else()
  target_compile_options(hud_path_font PRIVATE -Wall -Wextra -Wpedantic)
endif()
# sqrtf lives in libm on Linux and the BSDs; it is part of the C library on macOS and Windows
if(UNIX AND NOT APPLE)
  target_link_libraries(hud_path_font PUBLIC m)
endif()

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR OR HUD_PATH_FONT_ENABLE_TESTS)
  enable_testing()

  # the tests are the strictest consumer: C99 with no extensions, and every warning an error
  add_executable(hud-path-font-test tests/hud_path_font_test.c)
  target_link_libraries(hud-path-font-test PRIVATE hud_path_font)
  set_target_properties(hud-path-font-test PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
  if(MSVC)
    target_compile_options(hud-path-font-test PRIVATE /W4 /WX)
  else()
    target_compile_options(hud-path-font-test PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()
  add_test(NAME hud-path-font-test COMMAND hud-path-font-test)

  # each earlier copy against the package, both compiled into one program with the same flags; on GCC and Clang also
  # with floating-point contraction forced off and forced on, so the match does not depend on the compiler's default
  set(parity_modes default)
  if(NOT MSVC)
    include(CheckCCompilerFlag)
    check_c_compiler_flag(-ffp-contract=off HUD_PATH_FONT_HAS_FP_CONTRACT)
    if(HUD_PATH_FONT_HAS_FP_CONTRACT)
      list(APPEND parity_modes off fast)
    endif()
  endif()
  set(parity_index 0)
  foreach(dir IN LISTS HUD_PATH_FONT_PARITY_DIRS)
    get_filename_component(dir "${dir}" ABSOLUTE)
    if(NOT EXISTS "${dir}/hud_font.c")
      message(FATAL_ERROR "hud-path-font: ${dir} has no hud_font.c")
    endif()
    get_filename_component(label "${dir}" NAME)
    foreach(mode IN LISTS parity_modes)
      set(target hud-path-font-parity-${parity_index}-${mode})
      add_executable(${target} tests/parity_test.c "${dir}/hud_font.c" src/hud_path_font.c)
      target_include_directories(${target} PRIVATE "${dir}" "${CMAKE_CURRENT_SOURCE_DIR}/include")
      set_target_properties(${target} PROPERTIES C_STANDARD 99)
      if(NOT mode STREQUAL "default")
        target_compile_options(${target} PRIVATE -ffp-contract=${mode})
      endif()
      if(UNIX AND NOT APPLE)
        target_link_libraries(${target} PRIVATE m)
      endif()
      add_test(NAME hud-path-font-parity-${label}-${mode} COMMAND ${target} "${label} (fp-contract ${mode})")
    endforeach()
    math(EXPR parity_index "${parity_index} + 1")
  endforeach()
endif()
