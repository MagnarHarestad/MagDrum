[CmdletBinding()]
param(
    [ValidateRange(1, 64)]
    [int] $Parallel = [Math]::Min([Environment]::ProcessorCount, 8)
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = (Resolve-Path $PSScriptRoot).Path
$buildDirectory = Join-Path $repoRoot 'build/local-windows-x64'
$cachePath = Join-Path $buildDirectory 'CMakeCache.txt'
if (-not (Test-Path -LiteralPath $cachePath -PathType Leaf)) {
    throw 'The x64 build tree is not configured. Run Build-Windows.ps1 -SkipTests once to configure and create the release tree.'
}

$cache = Get-Content -LiteralPath $cachePath -Raw
if ($cache -notmatch '(?m)^CMAKE_GENERATOR_PLATFORM:INTERNAL=x64\s*$') {
    throw 'The configured build tree is not a Windows x64 build.'
}

$cmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
$cmakePath = if ($cmakeCommand) { $cmakeCommand.Source } else { $null }
if (-not $cmakePath) {
    $vswhereCandidates = @(
        (Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'),
        (Join-Path $env:ProgramFiles 'Microsoft Visual Studio/Installer/vswhere.exe')
    )
    $vswhere = $vswhereCandidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
    if ($vswhere) {
        $vsInstallPath = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if ($vsInstallPath) {
            $candidate = Join-Path $vsInstallPath 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
            if (Test-Path -LiteralPath $candidate) { $cmakePath = $candidate }
        }
    }
    if (-not $cmakePath -and (Test-Path -LiteralPath 'C:\Program Files\CMake\bin\cmake.exe')) {
        $cmakePath = 'C:\Program Files\CMake\bin\cmake.exe'
    }
}
if (-not $cmakePath) { throw 'CMake was not found on PATH or in Visual Studio.' }

Push-Location $repoRoot
try {
    & $cmakePath --build $buildDirectory --config Release --target MagDrum MagDrumPlugin_VST3 --parallel $Parallel
    if ($LASTEXITCODE -ne 0) { throw "Windows x64 plug-in build failed with exit code $LASTEXITCODE." }

    $app = Join-Path $buildDirectory 'MagDrum_artefacts/Release/MagDrum.exe'
    $bundle = Join-Path $buildDirectory 'MagDrumPlugin_artefacts/Release/VST3/MagDrum.vst3'
    $module = Join-Path $bundle 'Contents/x86_64-win/MagDrum.vst3'
    if (-not (Test-Path -LiteralPath $app -PathType Leaf)) { throw 'The matching-architecture MagDrum scanner helper is missing.' }
    if (-not (Test-Path -LiteralPath $module -PathType Leaf)) { throw 'The Windows x64 VST3 module was not produced.' }
    Copy-Item -LiteralPath $app -Destination (Join-Path (Split-Path -Parent $module) 'MagDrum.exe') -Force

    Write-Host "Fast Windows x64 VST3 build ready: $bundle"
    Write-Host 'This incremental path skips localization validation, CTest, packaging, publishing, and installation.'
    Write-Host 'At a testable milestone, run Build-Windows.ps1 for full validation and a release package.'
}
finally {
    Pop-Location
}
