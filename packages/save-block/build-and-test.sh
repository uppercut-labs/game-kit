#!/usr/bin/env bash
# Builds save-block's tests and runs them. macOS and Linux (Bash or zsh).
#
#   ./build-and-test.sh [build dir] [a game-kit checkout, for det-core]
#
# Needs CMake, Ninja and a C compiler. save-block uses det-core: inside a game-kit checkout it is the sibling folder.
# Run from a folder packed on its own, it is downloaded at its release tag (needs git and network), unless the second
# argument names a game-kit checkout to take det-core from.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
build="${1:-build-save-block}"
kit="${2:-}"
args=(-S "$here" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release)
[ -n "$kit" ] && args+=("-DFETCHCONTENT_SOURCE_DIR_DET_CORE=$kit")
cmake "${args[@]}"
cmake --build "$build"
ctest --test-dir "$build" --output-on-failure
