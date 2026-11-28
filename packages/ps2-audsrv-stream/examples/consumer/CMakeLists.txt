# A project using ps2-audsrv-stream from game-kit, pinned to a release tag. To try it against a checkout instead of the
# download:
#
#   cmake -S examples/consumer -B build-consumer -G Ninja -DFETCHCONTENT_SOURCE_DIR_PS2_AUDSRV_STREAM=<game-kit checkout>
#
# A real project leaves that option off; CMake clones game-kit at GIT_TAG into the build folder (needs git and network
# at configure time). On the console the same source files are compiled by the PS2SDK Makefile instead (README.md).
cmake_minimum_required(VERSION 3.20)
project(my_ps2_sound LANGUAGES C)

include(FetchContent)
FetchContent_Declare(ps2_audsrv_stream
  GIT_REPOSITORY https://github.com/uppercut-labs/game-kit.git
  GIT_TAG        ps2-audsrv-stream@1.0.0
  GIT_SHALLOW    TRUE
  SOURCE_SUBDIR  packages/ps2-audsrv-stream)
FetchContent_MakeAvailable(ps2_audsrv_stream)

add_executable(my-ps2-sound main.c)
target_link_libraries(my-ps2-sound PRIVATE pas)

enable_testing()
add_test(NAME my-ps2-sound COMMAND my-ps2-sound)
