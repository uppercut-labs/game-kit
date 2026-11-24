# port-sdl-input: a game port's input layer on SDL3 (README.md).
#
#   Standalone:   cmake -S packages/port-sdl-input -B build-psi -G Ninja && cmake --build build-psi && ctest --test-dir build-psi
#   In a project: add_subdirectory(<path>/port-sdl-input)   then   target_link_libraries(my-port PRIVATE psi)
#                 or fetch it at a tag with FetchContent (examples/consumer/CMakeLists.txt).
#
# Targets:
#   psi_core   the parts that need no SDL: the pad type, SOCD, stick steering, the button swap, the controller family
#              enum and the PlayStation slot table (include/psi/pad.h, family.h, layout.h)
#   psi        the SDL3 input layer (include/psi/psi.h); links psi_core
# Variables it gives the including project:
#   PSI_VERSION   the library's version
# Options: PSI_ENABLE_TESTS (ctest entries when included in another project; always on standalone).
#          PSI_SDL_TARGET (default SDL3::SDL3): the CMake target that gives SDL3's headers and library, for a build that
#          names its own (SDL3::SDL3-static, a target a parent project made).
#          PSI_REQUIRE_SDL (default ON): fail when SDL3 is missing. A project that builds without SDL too (a headless
#          tool that only wants the pad word) sets it OFF before including this: psi_core is then all there is.
cmake_minimum_required(VERSION 3.20)
if(NOT PROJECT_NAME)
  project(port_sdl_input LANGUAGES C)
endif()

# the version, read from include/psi/psi.h so it is written once
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/include/psi/psi.h" PSI_VERSION_LINE REGEX "^#define PSI_VERSION ")
string(REGEX MATCH "\"([^\"]+)\"" PSI_VERSION_MATCH "${PSI_VERSION_LINE}")
set(PSI_VERSION "${CMAKE_MATCH_1}")
set(PSI_VERSION "${PSI_VERSION}" CACHE INTERNAL "the port-sdl-input version")
message(STATUS "port-sdl-input ${PSI_VERSION}")

option(PSI_REQUIRE_SDL "Fail when SDL3 is not found; OFF builds only psi_core without it" ON)
set(PSI_SDL_TARGET "SDL3::SDL3" CACHE STRING "The CMake target for SDL3's headers and library")
if(NOT TARGET ${PSI_SDL_TARGET})
  if(PSI_REQUIRE_SDL)
    find_package(SDL3 3.2 REQUIRED)
  else()
    find_package(SDL3 3.2 QUIET)
  endif()
endif()

add_library(psi_core STATIC src/pad.c src/playstation.c)
add_library(psi::core ALIAS psi_core)
target_include_directories(psi_core PUBLIC include)
set_target_properties(psi_core PROPERTIES C_STANDARD 11 POSITION_INDEPENDENT_CODE ON)
if(NOT MSVC)
  target_compile_options(psi_core PRIVATE -Wall -Wextra)
endif()

add_executable(psi-pad-test tests/pad_test.c)
target_link_libraries(psi-pad-test PRIVATE psi_core)
if(NOT MSVC)
  target_compile_options(psi-pad-test PRIVATE -Wall -Wextra)
endif()
set(PSI_ENABLE_ANY_TESTS OFF)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR OR PSI_ENABLE_TESTS)
  set(PSI_ENABLE_ANY_TESTS ON)
  enable_testing()
  add_test(NAME psi-pad-test COMMAND psi-pad-test)
endif()

if(TARGET ${PSI_SDL_TARGET})
  add_library(psi STATIC src/psi.c src/bind.c)
  add_library(psi::psi ALIAS psi)
  target_link_libraries(psi PUBLIC psi_core ${PSI_SDL_TARGET})
  set_target_properties(psi PROPERTIES C_STANDARD 11 POSITION_INDEPENDENT_CODE ON)
  if(NOT MSVC)
    target_compile_options(psi PRIVATE -Wall -Wextra)
  endif()

  add_executable(psi-test tests/psi_test.c)
  target_link_libraries(psi-test PRIVATE psi)
  if(NOT MSVC)
    target_compile_options(psi-test PRIVATE -Wall -Wextra)
  endif()
  if(PSI_ENABLE_ANY_TESTS)
    add_test(NAME psi-test COMMAND psi-test "${CMAKE_CURRENT_BINARY_DIR}")
    # no window, GPU or controller: SDL's dummy drivers and its virtual joysticks
    set_tests_properties(psi-test PROPERTIES ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy")
  endif()
else()
  message(STATUS "port-sdl-input: SDL3 not found, building psi_core only")
endif()
