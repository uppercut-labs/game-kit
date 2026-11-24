#!/usr/bin/env bash
# The full check for the C packages on macOS and Linux (Bash or zsh): for every package under packages/ that has a
# CMakeLists.txt, configure, build and run its tests, then build its example consumer against the package's own source.
# Needs CMake, Ninja, a C compiler, Python 3 and whatever a package's README lists (SDL3 for the input packages).
#
#   ./tools/check-c.sh [install prefix for dependencies such as SDL3]
#
# Build output goes to build/<package>/ in this checkout.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
prefix="${1:-}"
extra=()
[ -n "$prefix" ] && extra+=("-DCMAKE_PREFIX_PATH=$prefix")
# Every example consumer takes every C package from this checkout, so a package that fetches another one at a tag
# builds against the source beside it.
overrides=()
for dep in "$root"/packages/*/; do
  [ -f "$dep/CMakeLists.txt" ] || continue
  overrides+=("-DFETCHCONTENT_SOURCE_DIR_$(basename "$dep" | tr '[:lower:]-' '[:upper:]_')=$root")
done
for pkg in "$root"/packages/*/; do
  [ -f "$pkg/CMakeLists.txt" ] || continue
  name="$(basename "$pkg")"
  echo "== $name"
  cmake -S "$pkg" -B "$root/build/$name" -G Ninja -DCMAKE_BUILD_TYPE=Release ${extra[@]+"${extra[@]}"}
  cmake --build "$root/build/$name"
  ctest --test-dir "$root/build/$name" --output-on-failure
  if [ -f "$pkg/examples/consumer/CMakeLists.txt" ]; then
    echo "== $name: the example consumer"
    cmake -S "$pkg/examples/consumer" -B "$root/build/$name-consumer" -G Ninja -DCMAKE_BUILD_TYPE=Release \
      "${overrides[@]}" ${extra[@]+"${extra[@]}"}
    cmake --build "$root/build/$name-consumer"
    ctest --test-dir "$root/build/$name-consumer" --output-on-failure
  fi
done
echo "check-c: all C packages passed"
