#!/usr/bin/env bash
# Builds libretro-guest-probe and its test and runs it. macOS and Linux (Bash or zsh).
#
#   ./build-and-test.sh [build dir [CMake arguments]]
#
# Needs CMake, Ninja, a C compiler, git and network at configure time (pad-replay and libretro.h are fetched), and SDL3
# (pass -DCMAKE_PREFIX_PATH=<SDL3 prefix>, or -DLGP_REQUIRE_SDL=OFF to build without a hardware-render context).
# The tests need no display, GPU or core.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
build="${1:-build-lgp}"
shift || true
# extra arguments go to CMake, for example -DCMAKE_PREFIX_PATH=<SDL3 prefix> or -DLGP_REQUIRE_SDL=OFF
cmake -S "$here" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release "$@"
cmake --build "$build"
ctest --test-dir "$build" --output-on-failure
