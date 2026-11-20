[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string] $PackagePath,
    [Parameter(Mandatory)]
    [ValidateRange(0, 99999)]
    [int] $BuildRevision,
    [string] $BuildDirectory = 'build\local-windows-release',
    [string] $ValidatorPath
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$buildRoot = if ([IO.Path]::IsPathRooted($BuildDirectory)) {
    [IO.Path]::GetFullPath($BuildDirectory)
} else {
    [IO.Path]::GetFullPath((Join-Path $repoRoot $BuildDirectory))
}
$repoPrefix = $repoRoot.TrimEnd([IO.Path]::DirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
if (-not $buildRoot.StartsWith($repoPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'BuildDirectory must stay inside the MagDrum repository.'
}
$resolvedPackage = (Resolve-Path -LiteralPath $PackagePath).Path
if ([IO.Path]::GetExtension($resolvedPackage) -ine '.zip') {
    throw 'Windows release acceptance requires a .zip package.'
}

$validatorCandidates = @()
if (-not [string]::IsNullOrWhiteSpace($ValidatorPath)) {
    $validatorCandidates += [IO.Path]::GetFullPath($ValidatorPath)
}
$validatorCandidates += @(
    (Join-Path $repoRoot 'build\local-vst3-validator\bin\Release\validator.exe'),
    (Join-Path $repoRoot 'build\vst3sdk-validator-win64\bin\Release\validator.exe')
)
$validator = $validatorCandidates | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
if (-not $validator) {
    throw 'Steinberg validator was not found. Supply -ValidatorPath; refusing to approve an unvalidated Windows release.'
}

$testRoot = Join-Path $env:TEMP ('MagDrum-Windows-Release-Acceptance-' + [guid]::NewGuid().ToString('N'))
$extractRoot = Join-Path $testRoot 'package'
$uiRoot = Join-Path $testRoot 'offscreen-ui'
New-Item -ItemType Directory -Path $extractRoot, $uiRoot | Out-Null
try {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $archive = [IO.Compression.ZipFile]::OpenRead($resolvedPackage)
    try {
        foreach ($entry in $archive.Entries) {
            $relativePath = $entry.FullName.Replace('/', [IO.Path]::DirectorySeparatorChar)
            if ([IO.Path]::IsPathRooted($relativePath) -or
                $relativePath.Split([IO.Path]::DirectorySeparatorChar) -contains '..') {
                throw "Unsafe path in Windows package: $($entry.FullName)"
            }
        }
    }
    finally { $archive.Dispose() }
    [IO.Compression.ZipFile]::ExtractToDirectory($resolvedPackage, $extractRoot)

    $bundlePath = Join-Path $extractRoot 'MagDrum.vst3'
    $modulePath = Join-Path $bundlePath 'Contents\x86_64-win\MagDrum.vst3'
    if (-not (Test-Path -LiteralPath $modulePath -PathType Leaf)) {
        throw 'Windows x64 VST3 module is missing from the release package.'
    }

    & (Join-Path $PSScriptRoot 'Test-InstalledWindowsVST3.ps1') `
        -PluginBundle $bundlePath `
        -BuildDirectory $buildRoot `
        -ReleasePackage $resolvedPackage `
        -ExpectedVersion "0.1.2.$BuildRevision"

    $pluginTest = Join-Path $buildRoot 'MagDrumPluginTests_artefacts\Release\MagDrumPluginTests.exe'
    if (-not (Test-Path -LiteralPath $pluginTest -PathType Leaf)) {
        throw "Offscreen plugin test executable is missing: $pluginTest"
    }
    & $pluginTest '--magdrum-render-ui' $uiRoot
    if ($LASTEXITCODE -ne 0) { throw "Offscreen VST rendering failed with exit code $LASTEXITCODE." }
    $images = @(Get-ChildItem -LiteralPath $uiRoot -File -Filter '*.png')
    if ($images.Count -lt 12) {
        throw "Expected at least 12 offscreen UI images, found $($images.Count)."
    }
    Write-Host "PASS: offscreen VST rendering produced $($images.Count) UI images."

    if (-not ('MagDrumReleaseErrorMode' -as [type])) {
        Add-Type -TypeDefinition 'using System; using System.Runtime.InteropServices; public static class MagDrumReleaseErrorMode { [DllImport("kernel32.dll")] public static extern uint SetErrorMode(uint mode); }'
    }
    [MagDrumReleaseErrorMode]::SetErrorMode(0x8003) | Out-Null
    $standardLog = Join-Path $testRoot 'validator-standard.log'
    $standardError = Join-Path $testRoot 'validator-standard.err'
    $standard = Start-Process -FilePath $validator -ArgumentList ('"{0}"' -f $modulePath) `
        -WorkingDirectory $testRoot -WindowStyle Hidden -PassThru -Wait `
        -RedirectStandardOutput $standardLog -RedirectStandardError $standardError
    $standardText = (Get-Content -LiteralPath $standardLog -Raw) + (Get-Content -LiteralPath $standardError -Raw)
    if ($standard.ExitCode -ne 0 -or $standardText -notmatch 'Result:\s*47 tests passed,\s*0 tests failed') {
        throw "Steinberg standard validation failed (exit $($standard.ExitCode)); see $standardLog."
    }
    Write-Host 'PASS: Steinberg VST 3.8.1 standard validation (47/47).'

    $extensiveLog = Join-Path $testRoot 'validator-extensive.log'
    $extensiveError = Join-Path $testRoot 'validator-extensive.err'
    $extensive = Start-Process -FilePath $validator -ArgumentList ('-e "{0}"' -f $modulePath) `
        -WorkingDirectory $testRoot -WindowStyle Hidden -PassThru -Wait `
        -RedirectStandardOutput $extensiveLog -RedirectStandardError $extensiveError
    $extensiveText = (Get-Content -LiteralPath $extensiveLog -Raw) + (Get-Content -LiteralPath $extensiveError -Raw)
    if ($extensive.ExitCode -ne 0 -or $extensiveText -notmatch 'Result:\s*537 tests passed,\s*0 tests failed') {
        throw "Steinberg extensive validation failed (exit $($extensive.ExitCode)); see $extensiveLog."
    }
    Write-Host 'PASS: Steinberg VST 3.8.1 extensive validation (537/537).'
    Write-Host "Windows release acceptance passed for version 0.1.2.$BuildRevision."
}
finally {
    if (Test-Path -LiteralPath $testRoot) {
        $resolvedTestRoot = (Resolve-Path -LiteralPath $testRoot).Path
        $tempPrefix = [IO.Path]::GetFullPath($env:TEMP).TrimEnd([IO.Path]::DirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
        if (-not $resolvedTestRoot.StartsWith($tempPrefix, [StringComparison]::OrdinalIgnoreCase) -or
            (Split-Path -Leaf $resolvedTestRoot) -notmatch '^MagDrum-Windows-Release-Acceptance-') {
            throw "Refusing to remove unexpected acceptance directory: $resolvedTestRoot"
        }
        Remove-Item -LiteralPath $resolvedTestRoot -Recurse -Force
    }
}
