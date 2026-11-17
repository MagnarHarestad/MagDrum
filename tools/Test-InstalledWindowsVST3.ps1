[CmdletBinding()]
param(
    [string] $PluginBundle = (Join-Path $env:LOCALAPPDATA 'Programs\Common\VST3\MagDrum.vst3'),
    [string] $BuildDirectory = 'build\local-windows-release',
    [string] $ExpectedVersion
)

$ErrorActionPreference = 'Stop'
if ($env:OS -ne 'Windows_NT') { throw 'This installed-plugin check runs on Windows only.' }

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$buildPath = if ([IO.Path]::IsPathRooted($BuildDirectory)) {
    [IO.Path]::GetFullPath($BuildDirectory)
} else {
    [IO.Path]::GetFullPath((Join-Path $repoRoot $BuildDirectory))
}
$repoPrefix = $repoRoot.TrimEnd([IO.Path]::DirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
if (-not $buildPath.StartsWith($repoPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'BuildDirectory must stay inside the MagDrum repository.'
}

$bundlePath = [IO.Path]::GetFullPath($PluginBundle)
$modulePath = Join-Path $bundlePath 'Contents\x86_64-win\MagDrum.vst3'
$scannerPath = Join-Path $bundlePath 'Contents\x86_64-win\MagDrum.exe'
$coreTestPath = Join-Path $buildPath 'Release\MagDrumTests.exe'
$pluginTestPath = Join-Path $buildPath 'MagDrumPluginTests_artefacts\Release\MagDrumPluginTests.exe'
foreach ($requiredPath in @($modulePath, $scannerPath, $coreTestPath, $pluginTestPath)) {
    if (-not (Test-Path -LiteralPath $requiredPath -PathType Leaf)) {
        throw "Required installed-plugin test file is missing: $requiredPath"
    }
}

$version = (Get-Item -LiteralPath $modulePath).VersionInfo.ProductVersion
if ([string]::IsNullOrWhiteSpace($version)) { throw 'The installed VST3 has no ProductVersion metadata.' }
if ($ExpectedVersion -and $version -ne $ExpectedVersion) {
    throw "Installed VST3 version $version does not match expected version $ExpectedVersion."
}

$moduleHash = (Get-FileHash -LiteralPath $modulePath -Algorithm SHA256).Hash
Write-Host "Installed MagDrum VST3: $version"
Write-Host "Module SHA-256: $moduleHash"
Write-Host 'Running the Windows core smoke suite and isolated 128-note VST3 scanner/render check...'
& $coreTestPath $modulePath $pluginTestPath $scannerPath
if ($LASTEXITCODE -ne 0) { throw "Installed VST3 smoke test failed with exit code $LASTEXITCODE." }
Write-Host 'PASS: installed VST3 restored sample state and rendered all 128 MIDI notes, including C1.'