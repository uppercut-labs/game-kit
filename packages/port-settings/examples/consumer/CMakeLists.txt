# A port's project using port-settings from game-kit, pinned to a release tag. To try it against a checkout instead
# of the download:
#
#   cmake -S examples/consumer -B build-consumer -G Ninja -DFETCHCONTENT_SOURCE_DIR_PORT_SETTINGS=<game-kit checkout>
#
# A real port leaves that option off; CMake clones game-kit at GIT_TAG into the build folder (needs git and network
# at configure time).
cmake_minimum_required(VERSION 3.20)
project(my_port_settings LANGUAGES C)

include(FetchContent)
FetchContent_Declare(port_settings
  GIT_REPOSITORY https://github.com/uppercut-labs/game-kit.git
  GIT_TAG        port-settings@0.1.0
  GIT_SHALLOW    TRUE
  SOURCE_SUBDIR  packages/port-settings)
FetchContent_MakeAvailable(port_settings)

add_executable(my-port-settings main.c)
target_link_libraries(my-port-settings PRIVATE pset)

enable_testing()
add_test(NAME my-port-settings COMMAND my-port-settings "${CMAKE_CURRENT_BINARY_DIR}")
