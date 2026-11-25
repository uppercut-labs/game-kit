# A port's project using port-sdl-input and port-settings from game-kit, each pinned to a release tag. To try it
# against a checkout instead of the download:
#
#   cmake -S examples/consumer -B build-consumer -G Ninja \
#     -DFETCHCONTENT_SOURCE_DIR_PORT_SDL_INPUT=<game-kit checkout> -DFETCHCONTENT_SOURCE_DIR_PORT_SETTINGS=<game-kit checkout>
#
# A real port leaves those options off; CMake clones game-kit at each GIT_TAG into the build folder (needs git and
# network at configure time). Put the two options in a cache file for an offline build.
cmake_minimum_required(VERSION 3.20)
project(my_port LANGUAGES C)

include(FetchContent)
FetchContent_Declare(port_settings
  GIT_REPOSITORY https://github.com/uppercut-labs/game-kit.git
  GIT_TAG        port-settings@0.1.0
  GIT_SHALLOW    TRUE
  SOURCE_SUBDIR  packages/port-settings)
FetchContent_Declare(port_sdl_input
  GIT_REPOSITORY https://github.com/uppercut-labs/game-kit.git
  GIT_TAG        port-sdl-input@0.1.0
  GIT_SHALLOW    TRUE
  SOURCE_SUBDIR  packages/port-sdl-input)
FetchContent_MakeAvailable(port_settings port_sdl_input)

add_executable(my-port main.c)
target_link_libraries(my-port PRIVATE psi pset)

enable_testing()
add_test(NAME my-port COMMAND my-port "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(my-port PROPERTIES ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy")
