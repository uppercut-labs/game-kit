# A tool's project using libretro-guest-probe from game-kit, pinned to a release tag. To try it against a checkout instead
# of the download:
#
#   cmake -S examples/consumer -B build-consumer -G Ninja -DFETCHCONTENT_SOURCE_DIR_LIBRETRO_GUEST_PROBE=<game-kit checkout> \
#         -DFETCHCONTENT_SOURCE_DIR_PAD_REPLAY=<game-kit checkout> -DCMAKE_PREFIX_PATH=<SDL3 prefix>
#
# A real tool leaves those options off; CMake clones game-kit at GIT_TAG into the build folder (needs git and network at
# configure time, and the library downloads libretro.h at a pinned commit). The library fetches pad-replay itself.
cmake_minimum_required(VERSION 3.20)
project(my_guest_probe LANGUAGES C)

include(FetchContent)
FetchContent_Declare(libretro_guest_probe
  GIT_REPOSITORY https://github.com/uppercut-labs/game-kit.git
  GIT_TAG        libretro-guest-probe@1.1.0
  GIT_SHALLOW    TRUE
  SOURCE_SUBDIR  packages/libretro-guest-probe)
FetchContent_MakeAvailable(libretro_guest_probe)

add_executable(my-guest-probe main.c)
target_link_libraries(my-guest-probe PRIVATE lgp)

enable_testing()
add_test(NAME my-guest-probe COMMAND my-guest-probe)
