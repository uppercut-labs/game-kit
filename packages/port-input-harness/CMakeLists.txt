# port-input-harness: SDL3 virtual controllers for testing a game port's input (README.md).
#
#   Standalone:   cmake -S packages/port-input-harness -B build-pih -G Ninja && cmake --build build-pih && ctest --test-dir build-pih
#   In a project: add_subdirectory(<path>/port-input-harness)   then   target_link_libraries(my-test PRIVATE pih)
#                 or fetch it at a tag with FetchContent (examples/consumer/CMakeLists.txt).
#
# Variables it gives the including project:
#   PIH_VERSION       the library's version
#   PIH_PROFILES_DIR  the controller profiles folder, for a test's --profiles
#   PIH_E2E_RUNNER    e2e/pih_e2e.py, the end-to-end runner
#   PIH_E2E_DIR       the folder it lives in (an adapter module imports Run from it)
# Options: PIH_ENABLE_TESTS (ctest entries when included in another project; always on standalone).
cmake_minimum_required(VERSION 3.20)
if(NOT PROJECT_NAME)
  project(port_input_harness LANGUAGES C)
endif()

# the version, read from include/pih/pih.h so it is written once
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/include/pih/pih.h" PIH_VERSION_LINE REGEX "^#define PIH_VERSION ")
string(REGEX MATCH "\"([^\"]+)\"" PIH_VERSION_MATCH "${PIH_VERSION_LINE}")
set(PIH_VERSION "${CMAKE_MATCH_1}")
set(PIH_VERSION "${PIH_VERSION}" CACHE INTERNAL "the port-input-harness version")
message(STATUS "port-input-harness ${PIH_VERSION}")

if(NOT TARGET SDL3::SDL3)
  find_package(SDL3 3.2 REQUIRED)
endif()

add_library(pih STATIC src/profile.c src/device.c src/map.c src/script.c src/runner.c)
add_library(pih::pih ALIAS pih)
target_include_directories(pih PUBLIC include)
target_link_libraries(pih PUBLIC SDL3::SDL3)
set_target_properties(pih PROPERTIES C_STANDARD 11)
if(NOT MSVC)
  target_compile_options(pih PRIVATE -Wall -Wextra)
endif()

set(PIH_PROFILES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/profiles" CACHE INTERNAL "the controller profiles")
set(PIH_E2E_DIR "${CMAKE_CURRENT_SOURCE_DIR}/e2e" CACHE INTERNAL "the end-to-end runner's folder")
set(PIH_E2E_RUNNER "${CMAKE_CURRENT_SOURCE_DIR}/e2e/pih_e2e.py" CACHE INTERNAL "the end-to-end runner")

# the library's own test: a made-up port (selftest/selftest.c) and its map, which also shows what an adapter is
add_executable(pih-selftest selftest/selftest.c)
target_link_libraries(pih-selftest PRIVATE pih)
if(NOT MSVC)
  target_compile_options(pih-selftest PRIVATE -Wall -Wextra)
endif()
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR OR PIH_ENABLE_TESTS)
  enable_testing()
  add_test(NAME pih-selftest COMMAND pih-selftest --profiles "${PIH_PROFILES_DIR}" --map "${CMAKE_CURRENT_SOURCE_DIR}/selftest/selftest.map")
  # the end-to-end runner's own test: a made-up port in Python (examples/e2e_adapter.py), no SDL, no game
  find_package(Python3 COMPONENTS Interpreter QUIET)
  if(Python3_Interpreter_FOUND)
    add_test(NAME pih-e2e-selftest COMMAND "${Python3_EXECUTABLE}" -I "${CMAKE_CURRENT_SOURCE_DIR}/selftest/e2e_selftest.py"
             "${CMAKE_CURRENT_SOURCE_DIR}")
  endif()
endif()
