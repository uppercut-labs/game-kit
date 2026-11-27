# A port's project using minimal-png from game-kit, pinned to a release tag. To try it against a checkout instead of the
# download:
#
#   cmake -S examples/consumer -B build-consumer -G Ninja -DFETCHCONTENT_SOURCE_DIR_MINIMAL_PNG=<game-kit checkout>
#
# A real port leaves that option off; CMake clones game-kit at GIT_TAG into the build folder (needs git and network at
# configure time).
cmake_minimum_required(VERSION 3.20)
project(my_png_dump LANGUAGES C)

include(FetchContent)
FetchContent_Declare(minimal_png
  GIT_REPOSITORY https://github.com/uppercut-labs/game-kit.git
  GIT_TAG        minimal-png@1.0.0
  GIT_SHALLOW    TRUE
  SOURCE_SUBDIR  packages/minimal-png)
FetchContent_MakeAvailable(minimal_png)

add_executable(my-png-dump main.c)
target_link_libraries(my-png-dump PRIVATE mpng)

enable_testing()
add_test(NAME my-png-dump COMMAND my-png-dump "${CMAKE_CURRENT_BINARY_DIR}")
