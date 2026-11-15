param(
    [ValidateSet('x64', 'ARM64')]
    [string] $Architecture = 'x64',
    [string] $BuildDirectory,
    [switch] $SkipTests
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = (Resolve-Path $PSScriptRoot).Path
$architectureKey = $Architecture.ToLowerInvariant()
$buildDirectory = if ([string]::IsNullOrWhiteSpace($BuildDirectory)) {
    Join-Path $repoRoot "build/local-windows-$architectureKey"
} else {
    $candidate = [System.IO.Path]::GetFullPath((Join-Path $repoRoot $BuildDirectory))
    $repoPrefix = $repoRoot.TrimEnd([System.IO.Path]::DirectorySeparatorChar) + [System.IO.Path]::DirectorySeparatorChar
    if (-not $candidate.StartsWith($repoPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw 'BuildDirectory must stay inside the MagDrum repository.'
    }
    $candidate
}
$dependencyDirectory = Join-Path $repoRoot 'build/_deps'
$vswhereCandidates = @(
    (Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'),
    (Join-Path $env:ProgramFiles 'Microsoft Visual Studio/Installer/vswhere.exe')
)
$vswhere = $vswhereCandidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if (-not $vswhere) { throw 'vswhere.exe was not found. Install Visual Studio with the C++ desktop workload.' }
$toolsetComponent = if ($Architecture -eq 'ARM64') {
    'Microsoft.VisualStudio.Component.VC.Tools.ARM64'
} else {
    'Microsoft.VisualStudio.Component.VC.Tools.x86.x64'
}
$vsRequirements = @('-latest', '-products', '*', '-requires', $toolsetComponent)
$vsInstallPath = & $vswhere @vsRequirements -property installationPath
$vsVersion = & $vswhere @vsRequirements -property installationVersion
if (-not $vsInstallPath -or -not $vsVersion) { throw "A Visual Studio installation with the $Architecture C++ toolset was not found. Install component $toolsetComponent." }
$vsMajorVersion = ([string]$vsVersion).Split('.')[0]
$generator = switch ($vsMajorVersion) {
    '18' { 'Visual Studio 18 2026' }
    '17' { 'Visual Studio 17 2022' }
    default { throw "Unsupported Visual Studio major version: $vsMajorVersion." }
}
$latestDirectory = Join-Path $repoRoot 'latest-publish'

& (Join-Path $repoRoot 'tools/Validate-Localization.ps1')
& (Join-Path $repoRoot 'tools/Test-WindowsInstallerGuard.ps1')

$cmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
$cmakePath = if ($cmakeCommand) { $cmakeCommand.Source } else { $null }
if (-not $cmakePath) {
    $candidates = @(
        (Join-Path $vsInstallPath 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'),
        'C:\Program Files\CMake\bin\cmake.exe'
    )
    $cmakePath = $candidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
}
if (-not $cmakePath) { throw 'CMake was not found. Install CMake and the Visual Studio C++ desktop workload.' }

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
        '-A', $Architecture,
        '-DBUILD_TESTING=ON'
    )
    $juceSource = Join-Path $dependencyDirectory 'juce-src'
    $sqliteSource = Join-Path $dependencyDirectory 'sqlite-src'
    if ((Test-Path -LiteralPath $juceSource) -and (Test-Path -LiteralPath $sqliteSource)) {
        $configureArguments += @("-DFETCHCONTENT_SOURCE_DIR_JUCE=$juceSource", "-DFETCHCONTENT_SOURCE_DIR_SQLITE=$sqliteSource")
    }
    & $cmakePath @configureArguments
    if ($LASTEXITCODE -ne 0) { throw "CMake configure failed with exit code $LASTEXITCODE." }

    # JUCE's generated Windows resource command does not depend on its refreshed Info.txt.
    # Recreate the generated .rc files so Windows file/product versions follow PROJECT_VERSION.
    foreach ($target in @('MagDrum', 'MagDrumPlugin')) {
        $resourceFile = Join-Path $buildDirectory "$target`_artefacts/JuceLibraryCode/${target}_resources.rc"
        if (Test-Path -LiteralPath $resourceFile) { Remove-Item -LiteralPath $resourceFile -Force }
    }

    $buildTargets = @('MagDrum', 'MagDrumPlugin_VST3')
    if (-not $SkipTests) { $buildTargets += @('MagDrumTests', 'MagDrumPluginTests') }
    & $cmakePath --build $buildDirectory --config Release --target $buildTargets --parallel 4
    if ($LASTEXITCODE -ne 0) { throw "Windows build failed with exit code $LASTEXITCODE." }

    if (-not $SkipTests -and $Architecture -eq 'x64') {
        & $ctestPath --test-dir $buildDirectory -C Release --output-on-failure
        if ($LASTEXITCODE -ne 0) { throw "CTest failed with exit code $LASTEXITCODE." }
    }
    elseif (-not $SkipTests) {
        Write-Host 'Windows ARM64 test binaries were cross-compiled but not executed; run CTest on native ARM64 Windows.'
    }

    $app = Get-ChildItem -LiteralPath $buildDirectory -Recurse -File -Filter 'MagDrum.exe' |
        Where-Object { $_.FullName -match '[\\/]MagDrum_artefacts[\\/]Release[\\/]MagDrum\.exe$' } |
        Select-Object -First 1
    $plugin = Get-ChildItem -LiteralPath $buildDirectory -Recurse -Directory -Filter 'MagDrum.vst3' |
        Where-Object { $_.FullName -match '[\\/]MagDrumPlugin_artefacts[\\/]Release[\\/]VST3[\\/]MagDrum\.vst3$' } |
        Select-Object -First 1
    if (-not $app) { throw 'The Windows standalone app was not produced.' }
    if (-not $plugin) { throw 'The Windows VST3 bundle was not produced.' }

    $pluginModule = Get-ChildItem -LiteralPath $plugin.FullName -Recurse -File -Filter 'MagDrum.vst3' |
        Select-Object -First 1
    if (-not $pluginModule) { throw 'The Windows VST3 module binary was not found inside its bundle.' }
    $pluginArchitectureDirectory = $pluginModule.Directory.FullName
    Copy-Item -LiteralPath $app.FullName -Destination (Join-Path $pluginArchitectureDirectory 'MagDrum.exe') -Force

    $stagingName = 'package-local-' + (Get-Date -Format 'yyyyMMdd-HHmmss')
    $stagingDirectory = Join-Path $buildDirectory $stagingName
    New-Item -ItemType Directory -Path $stagingDirectory | Out-Null
    Copy-Item -LiteralPath $app.FullName -Destination $stagingDirectory
    Copy-Item -LiteralPath $plugin.FullName -Destination (Join-Path $stagingDirectory 'MagDrum.vst3') -Recurse
    Copy-Item -LiteralPath (Join-Path $repoRoot 'installer/Install-MagDrum.ps1'), (Join-Path $repoRoot 'installer/README.txt') -Destination $stagingDirectory
    Copy-Item -LiteralPath (Join-Path $repoRoot 'LICENSE'), (Join-Path $repoRoot 'THIRD-PARTY-NOTICES.md') -Destination $stagingDirectory
    $juceSourceDirectory = Join-Path $dependencyDirectory 'juce-src'
    $thirdPartyLicenses = [ordered]@{
        'LICENSE.md' = 'JUCE-LICENSE.md'
        'modules/juce_audio_processors_headless/format_types/VST3_SDK/LICENSE.txt' = 'VST3-SDK-LICENSE.txt'
        'modules/juce_audio_formats/codecs/flac/Flac Licence.txt' = 'FLAC-LICENSE.txt'
        'modules/juce_audio_formats/codecs/oggvorbis/Ogg Vorbis Licence.txt' = 'Ogg-Vorbis-LICENSE.txt'
        'modules/juce_audio_formats/codecs/oggvorbis/libvorbis-1.3.7/COPYING' = 'libvorbis-COPYING.txt'
        'modules/juce_graphics/image_formats/jpglib/README' = 'JPEG-LICENSE.txt'
        'modules/juce_graphics/image_formats/pnglib/LICENSE' = 'PNG-LICENSE.txt'
        'modules/juce_core/zip/zlib/README' = 'zlib-LICENSE.txt'
        'modules/juce_graphics/fonts/harfbuzz/COPYING' = 'HarfBuzz-LICENSE.txt'
        'modules/juce_graphics/unicode/sheenbidi/LICENSE' = 'SheenBidi-LICENSE.txt'
    }
    foreach ($licensePath in $thirdPartyLicenses.Keys) {
        $sourceLicense = Join-Path $juceSourceDirectory $licensePath
        if (-not (Test-Path -LiteralPath $sourceLicense -PathType Leaf)) {
            throw "Required third-party license notice was not found in pinned JUCE: $licensePath"
        }
        Copy-Item -LiteralPath $sourceLicense -Destination (Join-Path $stagingDirectory $thirdPartyLicenses[$licensePath])
    }

    $zipPath = Join-Path $buildDirectory ($stagingName + "-Windows-$Architecture.zip")
    Compress-Archive -Path (Join-Path $stagingDirectory '*') -DestinationPath $zipPath
    New-Item -ItemType Directory -Force -Path $latestDirectory | Out-Null
    $latestZipPath = Join-Path $latestDirectory "MagDrum-Windows-$Architecture.zip"
    Copy-Item -LiteralPath $zipPath -Destination $latestZipPath -Force

    $hash = (Get-FileHash -LiteralPath $latestZipPath -Algorithm SHA256).Hash.ToLowerInvariant()
    Write-Host "Windows $Architecture package: $latestZipPath"
    Write-Host "SHA-256: $hash"
    Write-Host 'Publish this archive with gh release create when it is ready for distribution.'
}
finally {
    Pop-Location
}
