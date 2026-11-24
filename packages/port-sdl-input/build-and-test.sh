#!/usr/bin/env bash
# Builds the input layer and its tests and runs them. macOS and Linux (Bash or zsh).
#
#   ./build-and-test.sh [build dir] [SDL3 install prefix]
#
# Needs CMake, Ninja, a C compiler and SDL3 (brew install sdl3 cmake ninja; or apt install libsdl3-dev cmake ninja-build).
# The tests need no display or controller: SDL's dummy drivers and its virtual joysticks.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
build="${1:-build-psi}"
prefix="${2:-}"
args=(-S "$here" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release)
[ -n "$prefix" ] && args+=("-DCMAKE_PREFIX_PATH=$prefix")
cmake "${args[@]}"
cmake --build "$build"
ctest --test-dir "$build" --output-on-failure
