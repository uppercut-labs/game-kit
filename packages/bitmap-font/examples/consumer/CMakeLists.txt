# A project using bitmap-font from game-kit, pinned to a release tag. To try it against a checkout instead of the
# download:
#
#   cmake -S examples/consumer -B build-consumer -G Ninja -DFETCHCONTENT_SOURCE_DIR_BITMAP_FONT=<game-kit checkout>
#
# A real project leaves that option off; CMake clones game-kit at GIT_TAG into the build folder (needs git and network
# at configure time).
cmake_minimum_required(VERSION 3.20)
project(my_bitmap_font LANGUAGES C)

include(FetchContent)
FetchContent_Declare(bitmap_font
  GIT_REPOSITORY https://github.com/uppercut-labs/game-kit.git
  GIT_TAG        bitmap-font@1.1.0
  GIT_SHALLOW    TRUE
  SOURCE_SUBDIR  packages/bitmap-font)
FetchContent_MakeAvailable(bitmap_font)

add_executable(my-bitmap-font main.c)
target_link_libraries(my-bitmap-font PRIVATE bfont)

enable_testing()
add_test(NAME my-bitmap-font COMMAND my-bitmap-font)
