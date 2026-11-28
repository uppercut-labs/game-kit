# A project using kos-naomi-cabinet from game-kit, pinned to a release tag. To try it against a checkout instead of the
# download:
#
#   cmake -S examples/consumer -B build-consumer -G Ninja -DFETCHCONTENT_SOURCE_DIR_KOS_NAOMI_CABINET=<game-kit checkout>
#
# A real project leaves that option off; CMake clones game-kit at GIT_TAG into the build folder (needs git and network
# at configure time). On the console the same source files are compiled by the KallistiOS Makefile instead (README.md).
# The save-block adapter is not used here; KCAB_SAVE_BLOCK=OFF keeps this project from looking for save-block.
cmake_minimum_required(VERSION 3.20)
project(my_cabinet LANGUAGES C)

include(FetchContent)
set(KCAB_SAVE_BLOCK OFF CACHE STRING "")
FetchContent_Declare(kos_naomi_cabinet
  GIT_REPOSITORY https://github.com/uppercut-labs/game-kit.git
  GIT_TAG        kos-naomi-cabinet@1.0.1
  GIT_SHALLOW    TRUE
  SOURCE_SUBDIR  packages/kos-naomi-cabinet)
FetchContent_MakeAvailable(kos_naomi_cabinet)

add_executable(my-cabinet main.c)
target_link_libraries(my-cabinet PRIVATE kcab)

enable_testing()
add_test(NAME my-cabinet COMMAND my-cabinet)
