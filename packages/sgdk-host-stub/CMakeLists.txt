# sgdk-host-stub: host-test stand-ins for the SGDK calls a Mega Drive game makes (README.md).
#
#   Standalone:   cmake -S packages/sgdk-host-stub -B build-sgs -G Ninja && cmake --build build-sgs && ctest --test-dir build-sgs
#   In a project: add_subdirectory(<path>/sgdk-host-stub)   then   target_link_libraries(my-test PRIVATE sgdkstub)
#                 or fetch it at a tag with FetchContent (examples/consumer/CMakeLists.txt).
#
# Linking `sgdkstub` puts include/ on the include path, so the game's `#include <genesis.h>` finds the stub's file.
#
# Variables it gives the including project:
#   SGDKSTUB_VERSION  the library's version
# Options: SGDKSTUB_ENABLE_TESTS (ctest entries when included in another project; always on standalone).
cmake_minimum_required(VERSION 3.20)
if(NOT PROJECT_NAME)
  project(sgdk_host_stub LANGUAGES C)
endif()

# the version, read from include/sgdkstub/sgdkstub.h so it is written once
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/include/sgdkstub/sgdkstub.h" SGDKSTUB_VERSION_LINE REGEX "^#define SGDKSTUB_VERSION ")
string(REGEX MATCH "\"([^\"]+)\"" SGDKSTUB_VERSION_MATCH "${SGDKSTUB_VERSION_LINE}")
set(SGDKSTUB_VERSION "${CMAKE_MATCH_1}")
set(SGDKSTUB_VERSION "${SGDKSTUB_VERSION}" CACHE INTERNAL "the sgdk-host-stub version")
message(STATUS "sgdk-host-stub ${SGDKSTUB_VERSION}")

add_library(sgdkstub STATIC src/sgdkstub.c)
add_library(sgdkstub::sgdkstub ALIAS sgdkstub)
target_include_directories(sgdkstub PUBLIC include)
set_target_properties(sgdkstub PROPERTIES C_STANDARD 99 POSITION_INDEPENDENT_CODE ON)
if(NOT MSVC)
  target_compile_options(sgdkstub PRIVATE -Wall -Wextra -pedantic)
endif()

add_executable(sgdkstub-test tests/sgdkstub_test.c)
target_link_libraries(sgdkstub-test PRIVATE sgdkstub)
if(NOT MSVC)
  target_compile_options(sgdkstub-test PRIVATE -Wall -Wextra -pedantic)
endif()
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR OR SGDKSTUB_ENABLE_TESTS)
  enable_testing()
  add_test(NAME sgdkstub-test COMMAND sgdkstub-test)
  add_test(NAME sgdkstub-fresh-start COMMAND sgdkstub-test --fresh)
  # the default failure handler aborts: this run breaks a rule, catches the abort and exits 0; it exits 1 if the rule did not stop it
  add_test(NAME sgdkstub-aborts-on-violation COMMAND sgdkstub-test --violate)
endif()
