# A game's test project using det-core from game-kit, pinned to a release tag. This is also what the kit's check
# builds, with the package's own source standing in for the download (tools/check-c.sh and check-c.ps1):
#
#   cmake -S examples/consumer -B build-consumer -G Ninja -DFETCHCONTENT_SOURCE_DIR_DET_CORE=<game-kit checkout>
#
# A real project leaves that option off; CMake clones game-kit at GIT_TAG into the build folder (needs git and network
# at configure time). Put FETCHCONTENT_SOURCE_DIR_DET_CORE in a cache file for an offline build.
cmake_minimum_required(VERSION 3.20)
project(my_game_tests LANGUAGES C)

include(FetchContent)
FetchContent_Declare(det_core
  GIT_REPOSITORY https://github.com/uppercut-labs/game-kit.git
  GIT_TAG        det-core@1.0.0
  GIT_SHALLOW    TRUE
  SOURCE_SUBDIR  packages/det-core)
FetchContent_MakeAvailable(det_core)

add_executable(replay-test main.c)
target_link_libraries(replay-test PRIVATE det_core::det_core)

enable_testing()
add_test(NAME replay-is-deterministic COMMAND replay-test)
