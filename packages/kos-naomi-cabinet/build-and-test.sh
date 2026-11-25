#!/usr/bin/env bash
# Builds kos-naomi-cabinet's tests and runs them. macOS and Linux (Bash or zsh).
#
#   ./build-and-test.sh [build dir] [a game-kit checkout, for save-block]
#
# Needs CMake, Ninja and a C compiler. The save-block adapter and its test are built when save-block is found: inside a
# game-kit checkout it is the sibling folder. Run from a folder packed on its own, the adapter is left out (and says so),
# unless the second argument names a game-kit checkout to take save-block from. The rest needs nothing else.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
build="${1:-build-kcab}"
kit="${2:-}"
args=(-S "$here" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release)
[ -n "$kit" ] && args+=("-DFETCHCONTENT_SOURCE_DIR_SAVE_BLOCK=$kit")
cmake "${args[@]}"
cmake --build "$build"
ctest --test-dir "$build" --output-on-failure
