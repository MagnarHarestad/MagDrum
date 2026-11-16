[CmdletBinding()]
param(
    [string] $BuildDirectory = 'build/local-windows-dev-x64',
    [ValidateRange(1, 64)]
    [int] $Parallel = [Math]::Min([Environment]::ProcessorCount, 8)
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = (Resolve-Path $PSScriptRoot).Path
$buildPath = [System.IO.Path]::GetFullPath((Join-Path $repoRoot $BuildDirectory))
$repoPrefix = $repoRoot.TrimEnd([System.IO.Path]::DirectorySeparatorChar) + [System.IO.Path]::DirectorySeparatorChar
if (-not $buildPath.StartsWith($repoPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw 'BuildDirectory must stay inside the MagDrum repository.'
}

$vswhereCandidates = @(
    (Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'),
    (Join-Path $env:ProgramFiles 'Microsoft Visual Studio/Installer/vswhere.exe')
)
$vswhere = $vswhereCandidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if (-not $vswhere) { throw 'vswhere.exe was not found. Install Visual Studio with the C++ desktop workload.' }
$vsRequirements = @('-latest', '-products', '*', '-requires', 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64')
$vsInstallPath = & $vswhere @vsRequirements -property installationPath
$vsVersion = & $vswhere @vsRequirements -property installationVersion
if (-not $vsInstallPath -or -not $vsVersion) { throw 'Visual Studio with the Windows x64 C++ toolset was not found.' }
$generator = switch (([string]$vsVersion).Split('.')[0]) {
    '18' { 'Visual Studio 18 2026' }
    '17' { 'Visual Studio 17 2022' }
    default { throw "Unsupported Visual Studio major version: $vsVersion." }
}

$cmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
$cmakePath = if ($cmakeCommand) { $cmakeCommand.Source } else { $null }
if (-not $cmakePath) {
    $cmakeCandidate = Join-Path $vsInstallPath 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
    if (Test-Path -LiteralPath $cmakeCandidate) { $cmakePath = $cmakeCandidate }
}
if (-not $cmakePath -and (Test-Path -LiteralPath 'C:\Program Files\CMake\bin\cmake.exe')) {
    $cmakePath = 'C:\Program Files\CMake\bin\cmake.exe'
}
if (-not $cmakePath) { throw 'CMake was not found on PATH or in Visual Studio.' }

$cachePath = Join-Path $buildPath 'CMakeCache.txt'
$newBuildTree = -not (Test-Path -LiteralPath $cachePath -PathType Leaf)
if (-not $newBuildTree) {
    $cache = Get-Content -LiteralPath $cachePath -Raw
    if ($cache -notmatch '(?m)^CMAKE_GENERATOR_PLATFORM:INTERNAL=x64\s*$' -or
        $cache -notmatch '(?m)^CMAKE_GENERATOR:INTERNAL=Visual Studio' -or
        $cache -notmatch '(?m)^MAGDRUM_BUILD_MOBILE_PREVIEW:BOOL=OFF\s*$' -or
        $cache -notmatch '(?m)^BUILD_TESTING:BOOL=OFF\s*$' -or
        $cache -notmatch '(?m)^MAGDRUM_BUILD_CLASSIFIER_EVALUATION:BOOL=OFF\s*$') {
        throw 'The development build tree must use Windows x64 Visual Studio with tests, mobile preview, and classifier evaluation disabled.'
    }
}

Push-Location $repoRoot
try {
    $buildTimer = [System.Diagnostics.Stopwatch]::StartNew()
    if ($newBuildTree) {
        $configureArguments = @(
            '-S', $repoRoot,
            '-B', $buildPath,
            '-G', $generator,
            '-A', 'x64',
            '-DBUILD_TESTING=OFF',
            '-DMAGDRUM_BUILD_CLASSIFIER_EVALUATION=OFF',
            '-DMAGDRUM_BUILD_REVISION=0',
            '-DMAGDRUM_BUILD_MOBILE_PREVIEW=OFF'
        )
        $dependencyDirectory = Join-Path $repoRoot 'build/_deps'
        $juceSource = Join-Path $dependencyDirectory 'juce-src'
        $sqliteSource = Join-Path $dependencyDirectory 'sqlite-src'
        if ((Test-Path -LiteralPath $juceSource) -and (Test-Path -LiteralPath $sqliteSource)) {
            $configureArguments += @("-DFETCHCONTENT_SOURCE_DIR_JUCE=$juceSource", "-DFETCHCONTENT_SOURCE_DIR_SQLITE=$sqliteSource")
        }
        & $cmakePath @configureArguments
        if ($LASTEXITCODE -ne 0) { throw "Windows x64 development configure failed with exit code $LASTEXITCODE." }
    }

    # The standalone executable is shipped inside the bundle as an isolated scanner
    # helper. VST-only edits do not require relinking that helper, so build it only on
    # first use or when one of its own/shared sources has changed.
    $app = Join-Path $buildPath 'MagDrum_artefacts/Debug/MagDrum.exe'
    $appInputs = @(
        (Join-Path $repoRoot 'CMakeLists.txt'),
        (Join-Path $repoRoot 'src/app'),
        (Join-Path $repoRoot 'src/core'),
        (Join-Path $repoRoot 'src/ui'),
        (Join-Path $repoRoot 'assets/ui')
    )
    $appNeedsBuild = -not (Test-Path -LiteralPath $app -PathType Leaf)
    if (-not $appNeedsBuild) {
        $appTimestamp = (Get-Item -LiteralPath $app).LastWriteTimeUtc
        foreach ($appInputPath in $appInputs) {
            if (Test-Path -LiteralPath $appInputPath -PathType Leaf) {
                $item = Get-Item -LiteralPath $appInputPath
                if ($item.LastWriteTimeUtc -gt $appTimestamp) { $appNeedsBuild = $true; break }
            }
            elseif (Test-Path -LiteralPath $appInputPath -PathType Container) {
                $newerInput = Get-ChildItem -LiteralPath $appInputPath -Recurse -File -ErrorAction SilentlyContinue |
                    Where-Object { $_.LastWriteTimeUtc -gt $appTimestamp } |
                    Select-Object -First 1
                if ($null -ne $newerInput) { $appNeedsBuild = $true; break }
            }
        }
    }

    $buildTargets = if ($appNeedsBuild) { @('MagDrum', 'MagDrumPlugin_VST3') } else { @('MagDrumPlugin_VST3') }
    Write-Host "Building Debug targets: $($buildTargets -join ', ')"
    & $cmakePath --build $buildPath --config Debug --target $buildTargets --parallel $Parallel
    if ($LASTEXITCODE -ne 0) { throw "Windows x64 development build failed with exit code $LASTEXITCODE." }

    $bundle = Join-Path $buildPath 'MagDrumPlugin_artefacts/Debug/VST3/MagDrum.vst3'
    $module = Join-Path $bundle 'Contents/x86_64-win/MagDrum.vst3'
    if (-not (Test-Path -LiteralPath $app -PathType Leaf)) { throw 'The matching-architecture MagDrum scanner helper is missing.' }
    if (-not (Test-Path -LiteralPath $module -PathType Leaf)) { throw 'The Windows x64 Debug VST3 module was not produced.' }
    Copy-Item -LiteralPath $app -Destination (Join-Path (Split-Path -Parent $module) 'MagDrum.exe') -Force

    $buildTimer.Stop()
    Write-Host "Fast Windows x64 Debug VST3 build ready: $bundle"
    Write-Host "Incremental build elapsed: $($buildTimer.Elapsed.ToString('hh\:mm\:ss'))."
    Write-Host 'This development build uses the Debug configuration and skips CTest, packaging, publishing, and installation.'
    Write-Host 'Use Build-Windows.ps1 -BuildRevision <test-number> for the optimized Release build, full checks, tests, and package.'
}
finally {
    Pop-Location
}
