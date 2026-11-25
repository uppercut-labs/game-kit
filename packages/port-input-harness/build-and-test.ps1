# Builds the harness's library and its self-test and runs them. Windows, PowerShell 5.1 or 7.
#
#   .\build-and-test.ps1 [-Build <dir>] [-Compiler clang] [-SdlPrefix <SDL3 install prefix>]
#
# Needs CMake, Ninja, a C compiler and SDL3 (SDL3.dll beside the test is copied or found on PATH). On a machine with
# MSYS2 that is C:\msys64\ucrt64\bin first on PATH (pacman -S mingw-w64-ucrt-x86_64-{clang,cmake,ninja,SDL3}).
param(
    [string]$Build = "build-pih",
    [string]$Compiler = "clang",
    [string]$SdlPrefix = ""
)
$ErrorActionPreference = "Stop"
$cfg = @("-S", $PSScriptRoot, "-B", $Build, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_C_COMPILER=$Compiler")
if ($SdlPrefix) { $cfg += "-DCMAKE_PREFIX_PATH=$SdlPrefix" }
& cmake @cfg
if ($LASTEXITCODE) { exit $LASTEXITCODE }
& cmake --build $Build
if ($LASTEXITCODE) { exit $LASTEXITCODE }
& ctest --test-dir $Build --output-on-failure
exit $LASTEXITCODE
