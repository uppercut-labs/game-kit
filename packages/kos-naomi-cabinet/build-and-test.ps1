# Builds kos-naomi-cabinet's tests and runs them. Windows, PowerShell 5.1 or 7.
#
#   .\build-and-test.ps1 [-Build <dir>] [-Compiler clang] [-GameKit <a game-kit checkout, for save-block>]
#
# Needs CMake, Ninja and a C compiler (on a machine with MSYS2, C:\msys64\ucrt64\bin first on PATH). The save-block adapter
# and its test are built when save-block is found: inside a game-kit checkout it is the sibling folder. Run from a folder
# packed on its own, the adapter is left out (and says so), unless -GameKit names a game-kit checkout to take save-block from.
param(
    [string]$Build = "build-kcab",
    [string]$Compiler = "clang",
    [string]$GameKit = ""
)
$ErrorActionPreference = "Stop"
$cfg = @("-S", $PSScriptRoot, "-B", $Build, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_C_COMPILER=$Compiler")
if ($GameKit) { $cfg += "-DFETCHCONTENT_SOURCE_DIR_SAVE_BLOCK=$GameKit" }
& cmake @cfg
if ($LASTEXITCODE) { exit $LASTEXITCODE }
& cmake --build $Build
if ($LASTEXITCODE) { exit $LASTEXITCODE }
& ctest --test-dir $Build --output-on-failure
exit $LASTEXITCODE
