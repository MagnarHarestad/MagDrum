param(
    [switch] $SkipTests
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = (Resolve-Path $PSScriptRoot).Path
$buildDirectory = Join-Path $repoRoot 'build/local-windows-x64'
$dependencyDirectory = Join-Path $repoRoot 'build/_deps'
$vs18Path = 'C:\Program Files\Microsoft Visual Studio\18\Professional'
$vs2022Path = 'C:\Program Files\Microsoft Visual Studio\2022\Professional'
$generator = if (Test-Path -LiteralPath $vs18Path) { 'Visual Studio 18 2026' }
    elseif (Test-Path -LiteralPath $vs2022Path) { 'Visual Studio 17 2022' }
    else { throw 'Visual Studio with the C++ desktop workload was not found.' }
$latestDirectory = Join-Path $repoRoot 'latest-publish'

$cmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
$cmakePath = if ($cmakeCommand) { $cmakeCommand.Source } else { $null }
if (-not $cmakePath) {
    $candidates = @(
        'C:\Program Files\Microsoft Visual Studio\18\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe',
        'C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe',
        'C:\Program Files\CMake\bin\cmake.exe'
    )
    $cmakePath = $candidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
}
if (-not $cmakePath) {
    throw 'CMake was not found. Install CMake and the Visual Studio C++ desktop workload.'
}

$ctestPath = Join-Path (Split-Path -Parent $cmakePath) 'ctest.exe'
if (-not (Test-Path -LiteralPath $ctestPath)) {
    $ctestCommand = Get-Command ctest -ErrorAction SilentlyContinue
    if ($ctestCommand) { $ctestPath = $ctestCommand.Source }
    else { throw 'CTest was not found next to CMake or on PATH.' }
}

Push-Location $repoRoot
try {
    $configureArguments = @(
        '-S', $repoRoot,
        '-B', $buildDirectory,
        '-G', $generator,
        '-A', 'x64',
        '-DBUILD_TESTING=ON'
    )
    $juceSource = Join-Path $dependencyDirectory 'juce-src'
    $sqliteSource = Join-Path $dependencyDirectory 'sqlite-src'
    if ((Test-Path -LiteralPath $juceSource) -and (Test-Path -LiteralPath $sqliteSource)) {
        $configureArguments += @("-DFETCHCONTENT_SOURCE_DIR_JUCE=$juceSource", "-DFETCHCONTENT_SOURCE_DIR_SQLITE=$sqliteSource")
    }
    & $cmakePath @configureArguments
    if ($LASTEXITCODE -ne 0) { throw "CMake configure failed with exit code $LASTEXITCODE." }

    & $cmakePath --build $buildDirectory --config Release --target MagDrum MagDrumPlugin_VST3 MagDrumTests --parallel 4
    if ($LASTEXITCODE -ne 0) { throw "Windows build failed with exit code $LASTEXITCODE." }

    if (-not $SkipTests) {
        & $ctestPath --test-dir $buildDirectory -C Release --output-on-failure
        if ($LASTEXITCODE -ne 0) { throw "CTest failed with exit code $LASTEXITCODE." }
    }

    $app = Get-ChildItem -LiteralPath $buildDirectory -Recurse -File -Filter 'MagDrum.exe' |
        Where-Object { $_.FullName -match '[\\/]MagDrum_artefacts[\\/]Release[\\/]MagDrum\.exe$' } |
        Select-Object -First 1
    $plugin = Get-ChildItem -LiteralPath $buildDirectory -Recurse -Directory -Filter 'MagDrum.vst3' |
        Where-Object { $_.FullName -match '[\\/]MagDrumPlugin_artefacts[\\/]Release[\\/]VST3[\\/]MagDrum\.vst3$' } |
        Select-Object -First 1
    if (-not $app) { throw 'The Windows standalone app was not produced.' }
    if (-not $plugin) { throw 'The Windows VST3 bundle was not produced.' }

    $stagingName = 'package-local-' + (Get-Date -Format 'yyyyMMdd-HHmmss')
    $stagingDirectory = Join-Path $buildDirectory $stagingName
    New-Item -ItemType Directory -Path $stagingDirectory | Out-Null
    Copy-Item -LiteralPath $app.FullName -Destination $stagingDirectory
    Copy-Item -LiteralPath $plugin.FullName -Destination (Join-Path $stagingDirectory 'MagDrum.vst3') -Recurse
    Copy-Item -LiteralPath (Join-Path $repoRoot 'installer/Install-MagDrum.ps1'), (Join-Path $repoRoot 'installer/README.txt') -Destination $stagingDirectory

    $zipPath = Join-Path $buildDirectory ($stagingName + '-Windows-x64.zip')
    Compress-Archive -Path (Join-Path $stagingDirectory '*') -DestinationPath $zipPath
    New-Item -ItemType Directory -Force -Path $latestDirectory | Out-Null
    $latestZipPath = Join-Path $latestDirectory 'MagDrum-Windows-x64.zip'
    Copy-Item -LiteralPath $zipPath -Destination $latestZipPath -Force

    $hash = (Get-FileHash -LiteralPath $latestZipPath -Algorithm SHA256).Hash.ToLowerInvariant()
    Write-Host "Windows x64 package: $latestZipPath"
    Write-Host "SHA-256: $hash"
    Write-Host 'Publish this archive with gh release create when it is ready for distribution.'
}
finally {
    Pop-Location
}
