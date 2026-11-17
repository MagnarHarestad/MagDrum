[CmdletBinding()]
param(
    [string] $PluginBundle = (Join-Path $env:LOCALAPPDATA 'Programs\Common\VST3\MagDrum.vst3'),
    [string] $BuildDirectory = 'build\local-windows-release',
    [string] $ReleasePackage = 'latest-publish\MagDrum-Windows-x64.zip',
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

$defaultUserBundle = [IO.Path]::GetFullPath((Join-Path $env:LOCALAPPDATA 'Programs\Common\VST3\MagDrum.vst3'))
if ($bundlePath.Equals($defaultUserBundle, [StringComparison]::OrdinalIgnoreCase)) {
    $scanRoots = @(
        (Join-Path $env:LOCALAPPDATA 'Programs\Common\VST3'),
        (Join-Path $env:ProgramFiles 'Common Files\VST3')
    )
    $otherBundles = foreach ($scanRoot in $scanRoots) {
        if (Test-Path -LiteralPath $scanRoot -PathType Container) {
            Get-ChildItem -LiteralPath $scanRoot -Directory -Filter 'MagDrum.vst3' |
                Where-Object { -not $_.FullName.Equals($bundlePath, [StringComparison]::OrdinalIgnoreCase) }
        }
    }
    if ($otherBundles) {
        $paths = ($otherBundles | ForEach-Object FullName) -join [Environment]::NewLine
        throw "Another MagDrum VST3 bundle is present in a standard Windows scan root:`n$paths`nMove the older bundle outside the scan paths before testing in Cubase."
    }
}
$version = (Get-Item -LiteralPath $modulePath).VersionInfo.ProductVersion
if ([string]::IsNullOrWhiteSpace($version)) { throw 'The installed VST3 has no ProductVersion metadata.' }
if ($ExpectedVersion -and $version -ne $ExpectedVersion) {
    throw "Installed VST3 version $version does not match expected version $ExpectedVersion."
}

$moduleHash = (Get-FileHash -LiteralPath $modulePath -Algorithm SHA256).Hash
$packagePath = if ([IO.Path]::IsPathRooted($ReleasePackage)) {
    [IO.Path]::GetFullPath($ReleasePackage)
} else {
    [IO.Path]::GetFullPath((Join-Path $repoRoot $ReleasePackage))
}
if (Test-Path -LiteralPath $packagePath -PathType Leaf) {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $archive = [IO.Compression.ZipFile]::OpenRead($packagePath)
    try {
        $expectedEntryName = 'MagDrum.vst3/Contents/x86_64-win/MagDrum.vst3'
        $entry = $archive.Entries | Where-Object { $_.FullName.Replace([string][char]92, '/') -ieq $expectedEntryName } | Select-Object -First 1
        if ($null -eq $entry) { throw "Release package does not contain the x64 VST3 module: $packagePath" }
        $entryStream = $entry.Open()
        $sha256 = [Security.Cryptography.SHA256]::Create()
        try {
            $packageModuleHash = [BitConverter]::ToString($sha256.ComputeHash($entryStream)).Replace('-', '')
        } finally {
            $sha256.Dispose()
            $entryStream.Dispose()
        }
    } finally {
        $archive.Dispose()
    }
    if ($moduleHash -ne $packageModuleHash) {
        throw "Installed VST3 hash $moduleHash does not match release package module hash $packageModuleHash."
    }
    Write-Host "Package module SHA-256: $packageModuleHash"
} else {
    Write-Warning "Release package not found; installed-module hash comparison was skipped: $packagePath"
}
Write-Host "Installed MagDrum VST3: $version"
Write-Host "Module SHA-256: $moduleHash"
Write-Host 'Running the Windows core smoke suite and isolated 128-note VST3 scanner/render check...'
& $coreTestPath $modulePath $pluginTestPath $scannerPath
if ($LASTEXITCODE -ne 0) { throw "Installed VST3 smoke test failed with exit code $LASTEXITCODE." }
Write-Host 'PASS: installed VST3 restored sample state and rendered all 128 MIDI notes, including C1.'