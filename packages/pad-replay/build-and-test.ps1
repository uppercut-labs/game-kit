# Builds pad-replay and its test and runs it. Windows, PowerShell 5.1 or 7.
#
#   .\build-and-test.ps1 [-Build <dir>] [-Compiler clang]
#
# Needs CMake, Ninja and a C compiler (on a machine with MSYS2: C:\msys64\ucrt64\bin first on PATH,
# pacman -S mingw-w64-ucrt-x86_64-{clang,cmake,ninja}).
param(
    [string]$Build = "build-padr",
    [string]$Compiler = "clang"
)
$ErrorActionPreference = "Stop"
& cmake -S $PSScriptRoot -B $Build -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_C_COMPILER=$Compiler"
if ($LASTEXITCODE) { exit $LASTEXITCODE }
& cmake --build $Build
if ($LASTEXITCODE) { exit $LASTEXITCODE }
& ctest --test-dir $Build --output-on-failure
exit $LASTEXITCODE
