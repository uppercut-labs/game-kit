#!/usr/bin/env bash
# Builds hud-path-font and its tests and runs them. macOS and Linux (Bash or zsh).
#
#   ./build-and-test.sh [build dir]
#
# Needs CMake, Ninja and a C compiler.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
build="${1:-build-hud-path-font}"
cmake -S "$here" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build "$build"
ctest --test-dir "$build" --output-on-failure
