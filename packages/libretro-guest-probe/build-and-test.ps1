# Builds libretro-guest-probe and its tests and runs them. Windows, PowerShell 5.1 or 7.
#
#   .\build-and-test.ps1 [-Build <dir>] [-Compiler clang] [-CMakeArguments @("-DCMAKE_PREFIX_PATH=<SDL prefix>")]
#
# Needs CMake, Ninja, a C compiler, git and network at configure time (pad-replay and libretro.h are fetched), and SDL3
# or SDL2 unless -CMakeArguments gives -DLGP_REQUIRE_SDL=OFF. The tests need no display, GPU or core.
param(
    [string]$Build = "build-lgp",
    [string]$Compiler = "clang",
    [string[]]$CMakeArguments = @()
)
$ErrorActionPreference = "Stop"
& cmake -S $PSScriptRoot -B $Build -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_C_COMPILER=$Compiler" @CMakeArguments
if ($LASTEXITCODE) { exit $LASTEXITCODE }
& cmake --build $Build
if ($LASTEXITCODE) { exit $LASTEXITCODE }
& ctest --test-dir $Build --output-on-failure
exit $LASTEXITCODE
