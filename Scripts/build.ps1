<#
.SYNOPSIS
    Configures, builds and tests SideForge.

.DESCRIPTION
    The single repeatable entry point for the project. With no arguments it does
    a Release build of the plugin and the test runner, then runs the tests and
    fails the script if any of them fail.

.PARAMETER Config
    Debug or Release. Default: Release.

.PARAMETER Target
    What to build: All (plugin + tests), Plugin, or Tests. Default: All.

.PARAMETER SkipTests
    Build only; do not run the test runner.

.PARAMETER Clean
    Delete the build directory first.

.PARAMETER Install
    Copy the built VST3 into the system VST3 folder. Needs an elevated shell.

.EXAMPLE
    .\Scripts\build.ps1
.EXAMPLE
    .\Scripts\build.ps1 -Config Debug -Target Tests
.EXAMPLE
    .\Scripts\build.ps1 -Clean -Install
#>

[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')] [string] $Config = 'Release',
    [ValidateSet('All', 'Plugin', 'Tests')] [string] $Target = 'All',
    [switch] $SkipTests,
    [switch] $Clean,
    [switch] $Install
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$root      = Split-Path -Parent $PSScriptRoot
$buildDir  = Join-Path $root 'build'
$juceDir   = Join-Path $root 'JUCE'
$juceTag   = '8.0.15'

function Write-Step([string] $text) {
    Write-Host ''
    Write-Host "==> $text" -ForegroundColor Cyan
}

# --- JUCE ------------------------------------------------------------------
if (-not (Test-Path (Join-Path $juceDir 'CMakeLists.txt'))) {
    Write-Step "Cloning JUCE $juceTag (not checked in)"
    git clone --branch $juceTag --depth 1 https://github.com/juce-framework/JUCE.git $juceDir
    if ($LASTEXITCODE -ne 0) { throw 'Failed to clone JUCE.' }
}

# --- Configure -------------------------------------------------------------
if ($Clean -and (Test-Path $buildDir)) {
    Write-Step 'Removing the build directory'
    Remove-Item -Recurse -Force $buildDir
}

Write-Step 'Configuring'
cmake -S $root -B $buildDir -G 'Visual Studio 17 2022' -A x64
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed.' }

# --- Build -----------------------------------------------------------------
# SideForge_All is the target that produces the VST3 and the standalone; the
# bare SideForge target only builds the shared code library.
$targets = switch ($Target) {
    'Plugin' { @('SideForge_All') }
    'Tests'  { @('SideForgeTests') }
    default  { @('SideForge_All', 'SideForgeTests') }
}

foreach ($t in $targets) {
    Write-Step "Building $t ($Config)"
    cmake --build $buildDir --config $Config --target $t
    if ($LASTEXITCODE -ne 0) { throw "Build of $t failed." }
}

# --- Test ------------------------------------------------------------------
if (-not $SkipTests -and $Target -ne 'Plugin') {
    $runner = Join-Path $buildDir "SideForgeTests_artefacts\$Config\SideForgeTests.exe"

    if (-not (Test-Path $runner)) { throw "Test runner not found at $runner" }

    Write-Step 'Running tests'
    & $runner
    if ($LASTEXITCODE -ne 0) { throw 'Tests failed.' }
}

# --- Install ---------------------------------------------------------------
if ($Install) {
    $source = Join-Path $buildDir "SideForge_artefacts\$Config\VST3\SideForge.vst3"
    $dest   = 'C:\Program Files\Common Files\VST3'

    if (-not (Test-Path $source)) { throw "No VST3 at $source - build the plugin first." }

    Write-Step "Installing to $dest"
    # The .vst3 is a folder bundle; the whole thing has to be copied.
    Copy-Item -Recurse -Force $source (Join-Path $dest 'SideForge.vst3')
}

Write-Host ''
Write-Host 'Done.' -ForegroundColor Green

$artefacts = Join-Path $buildDir "SideForge_artefacts\$Config"
if (Test-Path $artefacts) {
    Get-ChildItem -Recurse $artefacts -Include '*.vst3', '*.exe' -ErrorAction SilentlyContinue |
        ForEach-Object { Write-Host "    $($_.FullName)" }
}
