# A port's test project using port-input-harness from game-kit, pinned to a release tag. This is also what the kit's
# check builds, with the harness's own source standing in for the download (see tools/check.ps1 and check.sh):
#
#   cmake -S examples/consumer -B build-consumer -G Ninja -DFETCHCONTENT_SOURCE_DIR_PORT_INPUT_HARNESS=<game-kit checkout>
#
# A real port leaves that option off; CMake clones game-kit at GIT_TAG into the build folder (needs git and network
# at configure time). Put FETCHCONTENT_SOURCE_DIR_PORT_INPUT_HARNESS in a cache file for an offline build.
cmake_minimum_required(VERSION 3.20)
project(my_port_tests LANGUAGES C)

include(FetchContent)
FetchContent_Declare(port_input_harness
  GIT_REPOSITORY https://github.com/uppercut-labs/game-kit.git
  GIT_TAG        port-input-harness@0.1.0
  GIT_SHALLOW    TRUE
  SOURCE_SUBDIR  packages/port-input-harness)
FetchContent_MakeAvailable(port_input_harness)

# my_port_adapter.c is the port's adapter (see selftest/selftest.c, a complete one); here the harness's own made-up
# port stands in for it, found through the package's source folder.
set(PIH_SRC "${port_input_harness_SOURCE_DIR}/packages/port-input-harness")
add_executable(my-port-controllers-test "${PIH_SRC}/selftest/selftest.c")
target_link_libraries(my-port-controllers-test PRIVATE pih)

enable_testing()
add_test(NAME my-port-controllers COMMAND my-port-controllers-test --profiles "${PIH_PROFILES_DIR}" --map "${PIH_SRC}/selftest/selftest.map")
# the end-to-end runner: python ${PIH_E2E_RUNNER} --adapter my_port_e2e.py --profiles ${PIH_PROFILES_DIR}
