# The full check for the C packages on Windows (PowerShell 5.1 or 7): for every package under packages\ that has a
# CMakeLists.txt, configure, build and run its tests, then build its example consumer against the package's own source.
# Needs CMake, Ninja, a C compiler, Python 3 and whatever a package's README lists (SDL3 for the input packages).
#
#   .\tools\check-c.ps1 [-Compiler clang] [-Prefix <install prefix for dependencies such as SDL3>]
#
# Build output goes to build\<package>\ in this checkout.
param(
    [string]$Compiler = "clang",
    [string]$Prefix = ""
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$common = @("-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_C_COMPILER=$Compiler")
if ($Prefix) { $common += "-DCMAKE_PREFIX_PATH=$Prefix" }

# Every example consumer takes every C package from this checkout, so a package that fetches another one at a tag
# builds against the source beside it.
$overrides = @(Get-ChildItem -Path (Join-Path $root "packages") -Directory |
    Where-Object { Test-Path (Join-Path $_.FullName "CMakeLists.txt") } |
    ForEach-Object { "-DFETCHCONTENT_SOURCE_DIR_" + $_.Name.ToUpperInvariant().Replace("-", "_") + "=$root" })

function Invoke-Native {
    param([string]$Exe, [string[]]$Arguments)
    & $Exe @Arguments
    if ($LASTEXITCODE) { throw "$Exe failed with exit code $LASTEXITCODE" }
}

foreach ($pkg in Get-ChildItem -Path (Join-Path $root "packages") -Directory) {
    if (-not (Test-Path (Join-Path $pkg.FullName "CMakeLists.txt"))) { continue }
    $name = $pkg.Name
    $build = Join-Path $root "build\$name"
    Write-Host "== $name"
    Invoke-Native cmake (@("-S", $pkg.FullName, "-B", $build) + $common)
    Invoke-Native cmake @("--build", $build)
    Invoke-Native ctest @("--test-dir", $build, "--output-on-failure")
    $consumer = Join-Path $pkg.FullName "examples\consumer"
    if (Test-Path (Join-Path $consumer "CMakeLists.txt")) {
        Write-Host "== ${name}: the example consumer"
        $cbuild = Join-Path $root "build\$name-consumer"
        Invoke-Native cmake (@("-S", $consumer, "-B", $cbuild) + $overrides + $common)
        Invoke-Native cmake @("--build", $cbuild)
        Invoke-Native ctest @("--test-dir", $cbuild, "--output-on-failure")
    }
}
Write-Host "check-c: all C packages passed"
