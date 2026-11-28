# det-core: deterministic-core primitives (README.md). Header-only, C99. No platform headers unless a console build
# sets DET_SDK_TYPES.
#
#   Standalone:   cmake -S packages/det-core -B build-det-core -G Ninja && cmake --build build-det-core && ctest --test-dir build-det-core
#   In a project: add_subdirectory(<path>/det-core)   then   target_link_libraries(my-game PRIVATE det_core::det_core)
#                 or fetch it at a tag with FetchContent (examples/consumer/CMakeLists.txt).
#
# Variables it gives the including project:
#   DET_CORE_VERSION  the package version
# Options: DET_CORE_ENABLE_TESTS (ctest entries when included in another project; always on standalone).
cmake_minimum_required(VERSION 3.20)
if(NOT PROJECT_NAME)
  project(det-core LANGUAGES C)
endif()

# the version, read from include/det/types.h so it is written once
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/include/det/types.h" DET_CORE_VERSION_LINE REGEX "^#define DET_VERSION ")
string(REGEX MATCH "\"([^\"]+)\"" DET_CORE_VERSION_MATCH "${DET_CORE_VERSION_LINE}")
set(DET_CORE_VERSION "${CMAKE_MATCH_1}")
set(DET_CORE_VERSION "${DET_CORE_VERSION}" CACHE INTERNAL "the det-core version")
message(STATUS "det-core ${DET_CORE_VERSION}")

add_library(det_core INTERFACE)
add_library(det_core::det_core ALIAS det_core)
target_include_directories(det_core INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_features(det_core INTERFACE c_std_99)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR OR DET_CORE_ENABLE_TESTS)
  enable_testing()
  foreach(t types fixed hash rng)
    add_executable(det-test-${t} tests/test_${t}.c)
    target_link_libraries(det-test-${t} PRIVATE det_core)
    # the tests are the strictest consumer: C99 with no extensions, and every warning an error
    set_target_properties(det-test-${t} PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
    if(MSVC)
      target_compile_options(det-test-${t} PRIVATE /W4 /WX)
    else()
      target_compile_options(det-test-${t} PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME det-${t} COMMAND det-test-${t})
  endforeach()

  # DET_SDK_TYPES as each console build sees it, against the stand-in SDK headers in tests/sdk-stub, and on the host
  foreach(platform host ps2 gamecube psp)
    add_executable(det-test-sdk-types-${platform} tests/test_sdk_types.c)
    target_link_libraries(det-test-sdk-types-${platform} PRIVATE det_core)
    target_include_directories(det-test-sdk-types-${platform} PRIVATE tests/sdk-stub)
    set_target_properties(det-test-sdk-types-${platform} PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
    if(platform STREQUAL "ps2")
      target_compile_definitions(det-test-sdk-types-${platform} PRIVATE _EE)
    elseif(platform STREQUAL "gamecube")
      target_compile_definitions(det-test-sdk-types-${platform} PRIVATE GEKKO)
    elseif(platform STREQUAL "psp")
      target_compile_definitions(det-test-sdk-types-${platform} PRIVATE __PSP__)
    endif()
    if(MSVC)
      target_compile_options(det-test-sdk-types-${platform} PRIVATE /W4 /WX)
    else()
      target_compile_options(det-test-sdk-types-${platform} PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME det-sdk-types-${platform} COMMAND det-test-sdk-types-${platform})
  endforeach()
endif()
