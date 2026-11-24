# A Genesis game's host tests using sgdk-host-stub from game-kit, pinned to a release tag. To try it against a checkout
# instead of the download (this is what the kit's own check does):
#
#   cmake -S examples/consumer -B build-consumer -G Ninja -DFETCHCONTENT_SOURCE_DIR_SGDK_HOST_STUB=<game-kit checkout>
#
# A real game leaves that option off; CMake clones game-kit at GIT_TAG into the build folder (needs git and network
# at configure time).
cmake_minimum_required(VERSION 3.20)
project(my_genesis_game_tests LANGUAGES C)

include(FetchContent)
FetchContent_Declare(sgdk_host_stub
  GIT_REPOSITORY https://github.com/uppercut-labs/game-kit.git
  GIT_TAG        sgdk-host-stub@0.1.0
  GIT_SHALLOW    TRUE
  SOURCE_SUBDIR  packages/sgdk-host-stub)
FetchContent_MakeAvailable(sgdk_host_stub)

# The game's own logic, the code that includes <genesis.h> in the ROM build. Here it builds against the stub, whose
# include folder comes with the sgdkstub target.
add_library(my-game STATIC game.c)
target_link_libraries(my-game PUBLIC sgdkstub)

add_executable(my-game-test game_test.c)
target_link_libraries(my-game-test PRIVATE my-game)

enable_testing()
add_test(NAME my-game-test COMMAND my-game-test)
