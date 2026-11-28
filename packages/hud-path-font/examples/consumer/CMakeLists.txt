# A project using hud-path-font from game-kit, pinned to a release tag. To try it against a checkout instead of the
# download:
#
#   cmake -S examples/consumer -B build-consumer -G Ninja -DFETCHCONTENT_SOURCE_DIR_HUD_PATH_FONT=<game-kit checkout>
#
# A real project leaves that option off; CMake clones game-kit at GIT_TAG into the build folder (needs git and network
# at configure time).
cmake_minimum_required(VERSION 3.20)
project(my_hud_path_font LANGUAGES C)

include(FetchContent)
FetchContent_Declare(hud_path_font
  GIT_REPOSITORY https://github.com/uppercut-labs/game-kit.git
  GIT_TAG        hud-path-font@1.0.0
  GIT_SHALLOW    TRUE
  SOURCE_SUBDIR  packages/hud-path-font)
FetchContent_MakeAvailable(hud_path_font)

add_executable(my-hud-path-font main.c)
target_link_libraries(my-hud-path-font PRIVATE hud_path_font)

enable_testing()
add_test(NAME my-hud-path-font COMMAND my-hud-path-font)
