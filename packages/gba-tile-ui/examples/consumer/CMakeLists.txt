# A game's host tests using gba-tile-ui from game-kit, pinned to a release tag. To try it against a checkout instead of
# the download (this is what the kit's own check does):
#
#   cmake -S examples/consumer -B build-consumer -G Ninja -DFETCHCONTENT_SOURCE_DIR_GBA_TILE_UI=<game-kit checkout>
#
# A real game leaves that option off; CMake clones game-kit at GIT_TAG into the build folder (needs git and network
# at configure time). On the console the game's own devkitARM Makefile compiles src/gba_tile_ui.c with -I include; the
# screen code in screen.c is the same file in both builds.
cmake_minimum_required(VERSION 3.20)
project(my_gba_game_tests LANGUAGES C)

include(FetchContent)
FetchContent_Declare(gba_tile_ui
  GIT_REPOSITORY https://github.com/uppercut-labs/game-kit.git
  GIT_TAG        gba-tile-ui@0.1.0
  GIT_SHALLOW    TRUE
  SOURCE_SUBDIR  packages/gba-tile-ui)
FetchContent_MakeAvailable(gba_tile_ui)

add_library(my-screens STATIC screen.c)
target_link_libraries(my-screens PUBLIC gtu)

add_executable(my-screens-test screen_test.c)
target_link_libraries(my-screens-test PRIVATE my-screens)

enable_testing()
add_test(NAME my-screens-test COMMAND my-screens-test)
