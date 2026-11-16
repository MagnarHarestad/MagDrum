param(
    [ValidateSet('x64', 'ARM64')]
    [string] $Architecture = 'x64',
    [string] $BuildDirectory,
    [switch] $SkipTests,
    [ValidateRange(1, 64)]
    [int] $Parallel = [Math]::Min([Environment]::ProcessorCount, 8),
    [ValidateRange(0, 99999)]
    [int] $BuildRevision = 0
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = (Resolve-Path $PSScriptRoot).Path
$architectureKey = $Architecture.ToLowerInvariant()
$buildDirectory = if ([string]::IsNullOrWhiteSpace($BuildDirectory)) {
    # Keep one warm x64 Release tree across test releases; clean trees rebuild all of JUCE.
    $defaultBuildDirectory = if ($Architecture -eq 'x64') {
        'build/local-windows-release-20261116'
    }
    else {
        "build/local-windows-$architectureKey"
    }
    Join-Path $repoRoot $defaultBuildDirectory
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
$runtimeArchitecture = if ($Architecture -eq 'ARM64') { 'arm64' } else { 'x64' }
$redistributableRoot = Join-Path $vsInstallPath 'VC/Redist/MSVC'
$redistributableDirectory = Get-ChildItem -LiteralPath $redistributableRoot -Directory -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -match '^\d+\.' } |
    Sort-Object { [version]$_.Name } -Descending |
    Select-Object -First 1
if (-not $redistributableDirectory) { throw 'The Visual C++ redistributable files were not found in Visual Studio.' }
$vcRedistPath = Join-Path $redistributableDirectory.FullName "vc_redist.$runtimeArchitecture.exe"
if (-not (Test-Path -LiteralPath $vcRedistPath -PathType Leaf)) {
    throw "The Visual C++ $runtimeArchitecture redistributable was not found: $vcRedistPath"
}
$vcRedistSignature = Get-AuthenticodeSignature -FilePath $vcRedistPath
if ($vcRedistSignature.Status -ne 'Valid' -or $vcRedistSignature.SignerCertificate.Subject -notlike '*CN=Microsoft Corporation*') {
    throw "The Visual C++ redistributable does not have a valid Microsoft signature: $vcRedistPath"
}
$vcRedistVersion = [version](Get-Item -LiteralPath $vcRedistPath).VersionInfo.FileVersion
$latestDirectory = Join-Path $repoRoot 'latest-publish'
$buildTesting = if ($SkipTests) { 'OFF' } else { 'ON' }

& (Join-Path $repoRoot 'tools/Validate-Localization.ps1')
& (Join-Path $repoRoot 'tools/Test-WindowsInstallerGuard.ps1')
& (Join-Path $repoRoot 'tools/Test-WindowsInstallerCleanup.ps1')
& (Join-Path $repoRoot 'tools/Test-WindowsInstallerPrerequisite.ps1')

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
    $configureTimer = [System.Diagnostics.Stopwatch]::StartNew()
    $configureArguments = @(
        '-S', $repoRoot,
        '-B', $buildDirectory,
        '-G', $generator,
        '-A', $Architecture,
        "-DMAGDRUM_BUILD_REVISION=$BuildRevision",
        "-DBUILD_TESTING=$buildTesting",
        '-DMAGDRUM_BUILD_MOBILE_PREVIEW=OFF'
    )
    $juceSource = Join-Path $dependencyDirectory 'juce-src'
    $sqliteSource = Join-Path $dependencyDirectory 'sqlite-src'
    if ((Test-Path -LiteralPath $juceSource) -and (Test-Path -LiteralPath $sqliteSource)) {
        $configureArguments += @("-DFETCHCONTENT_SOURCE_DIR_JUCE=$juceSource", "-DFETCHCONTENT_SOURCE_DIR_SQLITE=$sqliteSource")
    }
    & $cmakePath @configureArguments
    if ($LASTEXITCODE -ne 0) { throw "CMake configure failed with exit code $LASTEXITCODE." }
    $configureTimer.Stop()

    # JUCE does not always refresh the generated Windows .rc version fields when
    # PROJECT_VERSION changes. Update only stale files so the resource compile and
    # dependent relink happen when needed, without forcing them on every build.
    $productVersion = "0.1.2.$BuildRevision"
    $resourceFileVersion = "0,1,2,$BuildRevision"
    foreach ($target in @('MagDrum', 'MagDrumPlugin')) {
        $resourceFile = Join-Path $buildDirectory "$target`_artefacts/JuceLibraryCode/${target}_resources.rc"
        if (-not (Test-Path -LiteralPath $resourceFile -PathType Leaf)) {
            throw "JUCE's Windows resource file was not generated for $target."
        }
        $resourceText = [System.IO.File]::ReadAllText($resourceFile)
        $updatedResource = [regex]::Replace($resourceText, '(?m)^FILEVERSION[^\r\n]*', "FILEVERSION  $resourceFileVersion")
        foreach ($field in @('FileVersion', 'ProductVersion')) {
            $pattern = "(?m)^([ \t]*VALUE `"$field`",[ \t]*)`"[^`"]*`""
            if (([regex]::Matches($updatedResource, $pattern)).Count -ne 1) {
                throw "JUCE's Windows resource file for $target has no unique $field field."
            }
            $replacement = '$1' + [char]34 + $productVersion + '\' + '0' + [char]34
            $updatedResource = [regex]::Replace($updatedResource, $pattern, $replacement)
        }
        $expectedVersionValue = [string][char]34 + $productVersion + [string][char]92 + '0' + [string][char]34
        if (-not $updatedResource.Contains("FILEVERSION  $resourceFileVersion") -or
            -not $updatedResource.Contains('VALUE "FileVersion",  ' + $expectedVersionValue) -or
            -not $updatedResource.Contains('VALUE "ProductVersion",  ' + $expectedVersionValue)) {
            throw "Could not synchronize the Windows resource version for $target to $productVersion."
        }
        if ($updatedResource -cne $resourceText) {
            [System.IO.File]::WriteAllText($resourceFile, $updatedResource, [System.Text.UTF8Encoding]::new($false))
            Write-Host "Updated $target Windows resource version to $productVersion."
        }
    }

    $buildTargets = if ($SkipTests) {
        @('MagDrum', 'MagDrumPlugin_VST3')
    } else {
        # MagDrumTests depends on the app, VST3, and plugin-test executable.
        # One dependency-root invocation avoids repeated MSBuild startup while
        # excluding unrelated cached targets such as the mobile preview.
        @('MagDrumTests')
    }
    $compileTimer = [System.Diagnostics.Stopwatch]::StartNew()
    & $cmakePath --build $buildDirectory --config Release --target $buildTargets --parallel $Parallel
    if ($LASTEXITCODE -ne 0) { throw "Windows build failed with exit code $LASTEXITCODE." }
    $compileTimer.Stop()

    $testTimer = $null
    if (-not $SkipTests -and $Architecture -eq 'x64') {
        $testTimer = [System.Diagnostics.Stopwatch]::StartNew()
        & $ctestPath --test-dir $buildDirectory -C Release --output-on-failure
        if ($LASTEXITCODE -ne 0) { throw "CTest failed with exit code $LASTEXITCODE." }
        $testTimer.Stop()
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
    $actualProductVersion = [string](Get-Item -LiteralPath $pluginModule.FullName).VersionInfo.ProductVersion
    if ($actualProductVersion -ne $productVersion) {
        throw "Built VST3 version '$actualProductVersion' does not match requested product version '$productVersion'."
    }
    $pluginArchitectureDirectory = $pluginModule.Directory.FullName
    Copy-Item -LiteralPath $app.FullName -Destination (Join-Path $pluginArchitectureDirectory 'MagDrum.exe') -Force

    $packageTimer = [System.Diagnostics.Stopwatch]::StartNew()
    $stagingName = 'package-local-' + (Get-Date -Format 'yyyyMMdd-HHmmss')
    $stagingDirectory = Join-Path $buildDirectory $stagingName
    New-Item -ItemType Directory -Path $stagingDirectory | Out-Null
    Copy-Item -LiteralPath $app.FullName -Destination $stagingDirectory
    Copy-Item -LiteralPath $plugin.FullName -Destination (Join-Path $stagingDirectory 'MagDrum.vst3') -Recurse
    Copy-Item -LiteralPath (Join-Path $repoRoot 'installer/Install-MagDrum.ps1'), (Join-Path $repoRoot 'installer/MagDrumInstallHelpers.ps1'), (Join-Path $repoRoot 'installer/README.txt') -Destination $stagingDirectory
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

    Copy-Item -LiteralPath $vcRedistPath -Destination (Join-Path $stagingDirectory "vc_redist.$runtimeArchitecture.exe")
    $zipPath = Join-Path $buildDirectory ($stagingName + "-Windows-$Architecture.zip")
    Compress-Archive -Path (Join-Path $stagingDirectory '*') -DestinationPath $zipPath
    New-Item -ItemType Directory -Force -Path $latestDirectory | Out-Null
    $latestZipPath = Join-Path $latestDirectory "MagDrum-Windows-$Architecture.zip"
    Copy-Item -LiteralPath $zipPath -Destination $latestZipPath -Force

    $hash = (Get-FileHash -LiteralPath $latestZipPath -Algorithm SHA256).Hash.ToLowerInvariant()
    Write-Host "Windows $Architecture package: $latestZipPath"
    Write-Host "SHA-256: $hash"
    Write-Host "Product version: 0.1.2.$BuildRevision"
    Write-Host "Bundled Microsoft Visual C++ $runtimeArchitecture Redistributable: $vcRedistVersion."
    Write-Host "Build parallelism: $Parallel."
    Write-Host "CMake configure: $($configureTimer.Elapsed.ToString('hh\:mm\:ss')); compile/link: $($compileTimer.Elapsed.ToString('hh\:mm\:ss'))."
    if ($testTimer) { Write-Host "Tests: $($testTimer.Elapsed.ToString('hh\:mm\:ss'))." }
    $packageTimer.Stop()
    Write-Host "Packaging: $($packageTimer.Elapsed.ToString('hh\:mm\:ss'))."
    if ($SkipTests) {
        Write-Host 'CTest targets were not configured or run. Treat this package as a local iteration build; run the default full build before publishing.'
    }
    Write-Host 'Publish this archive with gh release create when it is ready for distribution.'
}
finally {
    Pop-Location
}
