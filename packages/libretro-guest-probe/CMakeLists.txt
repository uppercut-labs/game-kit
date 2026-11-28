# libretro-guest-probe: run a libretro core headless, feed it scripted pad input, read the guest's memory (README.md).
#
#   Standalone:   cmake -S packages/libretro-guest-probe -B build-lgp -G Ninja && cmake --build build-lgp && ctest --test-dir build-lgp
#   In a project: add_subdirectory(<path>/libretro-guest-probe)   then   target_link_libraries(my-tool PRIVATE lgp)
#                 or fetch it at a tag with FetchContent (examples/consumer/CMakeLists.txt).
#
# Needs: pad-replay (fetched at its tag, or taken from the checkout beside this folder), libretro.h (downloaded at a pinned
# commit and SHA-256, see cmake/LgpLibretroHeader.cmake) and, for a core that wants a hardware OpenGL context, SDL3 or
# SDL2.
#
# Targets:
#   lgp                       the library: option table, input bridge, memory probe, guest addresses, argument parser,
#                             the host with its frame hook and capture (lgp/lgp.h)
#   libretro-guest-probe      the command line tool
# Variables it gives the including project:
#   LGP_VERSION   the library's version
# Options: LGP_ENABLE_TESTS (ctest entries when included in another project; always on standalone).
#          LGP_REQUIRE_SDL (default ON): fail when no SDL is found. OFF builds without one: a core that asks for an OpenGL
#          context is then refused one, and software cores still run.
#          LGP_SDL (default auto): which SDL makes the hidden OpenGL context: auto (SDL3, else SDL2), SDL3 or SDL2.
#          LGP_LIBRETRO_HEADER: a local copy of the pinned libretro.h, for a build with no network.
#          LGP_PAD_REPLAY_TAG (default pad-replay@1.0.0): the pad-replay release to fetch when none is beside this folder.
cmake_minimum_required(VERSION 3.20)
if(NOT PROJECT_NAME)
  project(libretro_guest_probe LANGUAGES C)
endif()

# the version, read from include/lgp/lgp.h so it is written once
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/include/lgp/lgp.h" LGP_VERSION_LINE REGEX "^#define LGP_VERSION ")
string(REGEX MATCH "\"([^\"]+)\"" LGP_VERSION_MATCH "${LGP_VERSION_LINE}")
set(LGP_VERSION "${CMAKE_MATCH_1}")
set(LGP_VERSION "${LGP_VERSION}" CACHE INTERNAL "the libretro-guest-probe version")
message(STATUS "libretro-guest-probe ${LGP_VERSION}")

# pad-replay: from the game-kit checkout this folder sits in when there is one, else at its release tag.
if(NOT TARGET padr)
  include(FetchContent)
  set(LGP_PAD_REPLAY_TAG "pad-replay@1.0.0" CACHE STRING "The pad-replay release tag to fetch")
  get_filename_component(_lgp_kit_root "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
  if(NOT DEFINED FETCHCONTENT_SOURCE_DIR_PAD_REPLAY AND EXISTS "${_lgp_kit_root}/packages/pad-replay/CMakeLists.txt")
    set(FETCHCONTENT_SOURCE_DIR_PAD_REPLAY "${_lgp_kit_root}" CACHE PATH "pad-replay from the checkout beside this package")
  endif()
  FetchContent_Declare(pad_replay
    GIT_REPOSITORY https://github.com/uppercut-labs/game-kit.git
    GIT_TAG        ${LGP_PAD_REPLAY_TAG}
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  packages/pad-replay)
  FetchContent_MakeAvailable(pad_replay)
endif()

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/LgpLibretroHeader.cmake")

option(LGP_REQUIRE_SDL "Fail when no SDL is found; OFF builds without a hardware-render context" ON)
set(LGP_SDL "auto" CACHE STRING "Which SDL makes the hidden OpenGL context: auto (SDL3, else SDL2), SDL3 or SDL2")
set_property(CACHE LGP_SDL PROPERTY STRINGS auto SDL3 SDL2)
set(LGP_SDL_TARGET "")
if(NOT LGP_SDL STREQUAL "SDL2")
  if(NOT TARGET SDL3::SDL3)
    find_package(SDL3 3.2 QUIET)
  endif()
  if(TARGET SDL3::SDL3)
    set(LGP_SDL_TARGET SDL3::SDL3)
  endif()
endif()
if(NOT LGP_SDL_TARGET AND NOT LGP_SDL STREQUAL "SDL3")
  if(NOT TARGET SDL2::SDL2)
    find_package(SDL2 2.0.18 QUIET)
  endif()
  if(TARGET SDL2::SDL2)
    set(LGP_SDL_TARGET SDL2::SDL2)
  endif()
endif()
if(NOT LGP_SDL_TARGET AND LGP_REQUIRE_SDL)
  message(FATAL_ERROR "libretro-guest-probe: no SDL found (LGP_SDL=${LGP_SDL}). Give -DCMAKE_PREFIX_PATH=<SDL3 or SDL2 "
                      "prefix>, or -DLGP_REQUIRE_SDL=OFF to build without a hardware-render context.")
endif()
if(LGP_SDL_TARGET)
  message(STATUS "libretro-guest-probe: hardware-render context from ${LGP_SDL_TARGET}")
else()
  message(STATUS "libretro-guest-probe: no SDL, so no hardware-render context")
endif()

set(LGP_SOURCES src/options.c src/input.c src/probe.c src/ram.c src/frames.c src/args.c src/host.c)
if(LGP_SDL_TARGET)
  list(APPEND LGP_SOURCES src/gl_sdl.c)
else()
  list(APPEND LGP_SOURCES src/gl_none.c)
endif()
add_library(lgp STATIC ${LGP_SOURCES})
add_library(lgp::lgp ALIAS lgp)
target_include_directories(lgp PUBLIC include PRIVATE src "${LGP_LIBRETRO_INCLUDE_DIR}")
target_link_libraries(lgp PUBLIC padr)
if(LGP_SDL_TARGET)
  target_link_libraries(lgp PRIVATE ${LGP_SDL_TARGET})
  if(LGP_SDL_TARGET STREQUAL "SDL2::SDL2")
    target_compile_definitions(lgp PRIVATE LGP_SDL2)
  endif()
endif()
set_target_properties(lgp PROPERTIES C_STANDARD 11 POSITION_INDEPENDENT_CODE ON)
if(NOT WIN32)
  target_link_libraries(lgp PUBLIC ${CMAKE_DL_LIBS})
endif()
if(NOT MSVC)
  target_compile_options(lgp PRIVATE -Wall -Wextra)
endif()

add_executable(libretro-guest-probe src/main.c)
target_link_libraries(libretro-guest-probe PRIVATE lgp)
set_target_properties(libretro-guest-probe PROPERTIES C_STANDARD 11)
if(NOT MSVC)
  target_compile_options(libretro-guest-probe PRIVATE -Wall -Wextra)
endif()

# the tests: none of them needs a display, a GPU or a real core
set(LGP_ENABLE_ANY_TESTS OFF)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR OR LGP_ENABLE_TESTS)
  set(LGP_ENABLE_ANY_TESTS ON)
  enable_testing()
endif()

foreach(t options input probe args ram)
  add_executable(lgp-${t}-test tests/${t}_test.c)
  target_link_libraries(lgp-${t}-test PRIVATE lgp)
  target_include_directories(lgp-${t}-test PRIVATE "${LGP_LIBRETRO_INCLUDE_DIR}")
  if(NOT MSVC)
    target_compile_options(lgp-${t}-test PRIVATE -Wall -Wextra)
  endif()
  if(LGP_ENABLE_ANY_TESTS)
    add_test(NAME lgp-${t}-test COMMAND lgp-${t}-test "${CMAKE_CURRENT_BINARY_DIR}")
  endif()
endforeach()

# a fake libretro core (no emulation) so the host runs end to end without a real emulator
add_library(lgp_fake_core MODULE tests/fake_core.c)
set_target_properties(lgp_fake_core PROPERTIES PREFIX "" OUTPUT_NAME "lgp_fake_core" C_STANDARD 11)
target_include_directories(lgp_fake_core PRIVATE "${LGP_LIBRETRO_INCLUDE_DIR}")
add_executable(lgp-host-test tests/host_test.c)
target_link_libraries(lgp-host-test PRIVATE lgp)
target_include_directories(lgp-host-test PRIVATE "${LGP_LIBRETRO_INCLUDE_DIR}")
add_dependencies(lgp-host-test lgp_fake_core)
if(NOT MSVC)
  target_compile_options(lgp-host-test PRIVATE -Wall -Wextra)
  target_compile_options(lgp_fake_core PRIVATE -Wall -Wextra)
endif()
if(LGP_ENABLE_ANY_TESTS)
  add_test(NAME lgp-host-test COMMAND lgp-host-test "$<TARGET_FILE:lgp_fake_core>" "${CMAKE_CURRENT_BINARY_DIR}")
endif()

# the closed-loop example: the frame hook reading a guest record and steering, with capture. The test drives the fake
# core with it; the README shows it against a real core and game.
add_executable(lgp-closed-loop examples/closed-loop/main.c)
target_link_libraries(lgp-closed-loop PRIVATE lgp)
set_target_properties(lgp-closed-loop PROPERTIES C_STANDARD 11)
if(NOT MSVC)
  target_compile_options(lgp-closed-loop PRIVATE -Wall -Wextra)
endif()
if(LGP_ENABLE_ANY_TESTS)
  add_dependencies(lgp-closed-loop lgp_fake_core)
  add_test(NAME lgp-closed-loop-example
           COMMAND lgp-closed-loop --record 0x8c000200 --ram-mask 0x00ffffff --ram-base 0x8c000000 --tick 0x8c000100
                   --words 4 --ready-word 0=7 --screen-word 1 --play-screen 1 --x-word 2 --x-min -2 --x-max 2
                   --fire r2 --play-frames 60 --video "${CMAKE_CURRENT_BINARY_DIR}/closed-loop.rgb"
                   --audio "${CMAKE_CURRENT_BINARY_DIR}/closed-loop.raw" --capture-frames 20 --max-frames 400
                   "$<TARGET_FILE:lgp_fake_core>" - "${CMAKE_CURRENT_BINARY_DIR}/system")
endif()
