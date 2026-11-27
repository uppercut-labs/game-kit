# A game's test project using save-block from game-kit, pinned to a release tag. This is also what the kit's check
# builds, with the package's own source standing in for the download (tools/check-c.sh and check-c.ps1):
#
#   cmake -S examples/consumer -B build-consumer -G Ninja -DFETCHCONTENT_SOURCE_DIR_SAVE_BLOCK=<game-kit checkout>
#
# A real project leaves that option off; CMake clones game-kit at GIT_TAG into the build folder (needs git and network
# at configure time). Put FETCHCONTENT_SOURCE_DIR_SAVE_BLOCK in a cache file for an offline build.
#
# save-block needs det-core for its checksum. The clone of game-kit holds both, and save-block finds det-core next to
# itself. A project that also uses det-core on its own at another tag declares it first, before this one; save-block
# then uses that one:
#
#   FetchContent_Declare(det_core GIT_REPOSITORY https://github.com/uppercut-labs/game-kit.git GIT_TAG det-core@1.0.0
#                        GIT_SHALLOW TRUE SOURCE_SUBDIR packages/det-core)
#   FetchContent_MakeAvailable(det_core)
cmake_minimum_required(VERSION 3.20)
project(my_game_tests LANGUAGES C)

include(FetchContent)
FetchContent_Declare(save_block
  GIT_REPOSITORY https://github.com/uppercut-labs/game-kit.git
  GIT_TAG        save-block@1.0.0
  GIT_SHALLOW    TRUE
  SOURCE_SUBDIR  packages/save-block)
FetchContent_MakeAvailable(save_block)

add_executable(save-test main.c)
target_link_libraries(save-test PRIVATE save_block::save_block)

enable_testing()
add_test(NAME save-survives-a-power-cut COMMAND save-test)
