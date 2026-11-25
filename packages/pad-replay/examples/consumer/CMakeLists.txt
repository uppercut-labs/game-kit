# A port's project using pad-replay from game-kit, pinned to a release tag. To try it against a checkout instead of
# the download:
#
#   cmake -S examples/consumer -B build-consumer -G Ninja -DFETCHCONTENT_SOURCE_DIR_PAD_REPLAY=<game-kit checkout>
#
# A real port leaves that option off; CMake clones game-kit at GIT_TAG into the build folder (needs git and network
# at configure time).
cmake_minimum_required(VERSION 3.20)
project(my_port_pad_replay LANGUAGES C)

include(FetchContent)
FetchContent_Declare(pad_replay
  GIT_REPOSITORY https://github.com/uppercut-labs/game-kit.git
  GIT_TAG        pad-replay@0.1.0
  GIT_SHALLOW    TRUE
  SOURCE_SUBDIR  packages/pad-replay)
FetchContent_MakeAvailable(pad_replay)

add_executable(my-port-replay main.c)
target_link_libraries(my-port-replay PRIVATE padr)

enable_testing()
add_test(NAME my-port-replay COMMAND my-port-replay "${CMAKE_CURRENT_BINARY_DIR}")
