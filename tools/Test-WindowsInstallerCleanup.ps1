$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

. (Join-Path $PSScriptRoot '../installer/MagDrumInstallHelpers.ps1')

$testRoot = Join-Path $env:TEMP ("MagDrum-Installer-Cleanup-Test-" + [guid]::NewGuid().ToString('N'))
$programFiles = Join-Path $testRoot 'Program Files'
$localAppData = Join-Path $testRoot 'Local AppData'
$backupParent = Join-Path $testRoot 'Backups'
try {
    $roots = @(Get-MagDrumDefaultVst3ScanRoots -ProgramFilesPath $programFiles -LocalAppDataPath $localAppData)
    $systemRoot = Join-Path $programFiles 'Common Files\VST3'
    $userRoot = Join-Path $localAppData 'Programs\Common\VST3'
    if ($roots.Count -ne 2 -or $roots[0] -ne $systemRoot -or $roots[1] -ne $userRoot) {
        throw 'The default VST3 scan roots do not match JUCE Windows x64 locations.'
    }

    $active = Join-Path $systemRoot 'MagDrum.vst3'
    $oldSystem = Join-Path $systemRoot 'MagDrum-old.vst3'
    $oldUser = Join-Path $userRoot 'MagDrum.vst3'
    $otherVendor = Join-Path $userRoot 'MagNoise.vst3'
    foreach ($bundle in @($active, $oldSystem, $oldUser, $otherVendor)) {
        New-Item -ItemType Directory -Path $bundle -Force | Out-Null
        Set-Content -LiteralPath (Join-Path $bundle 'marker.txt') -Value $bundle
    }

    $result = Move-OldMagDrumVst3Bundles -Destination $active -BackupParent $backupParent -ScanRoots $roots
    if ($result.Moved.Count -ne 2) { throw "Expected two old MagDrum bundles to be backed up, moved $($result.Moved.Count)." }
    if ($result.Failed.Count -ne 0) { throw "Unexpected cleanup failures: $($result.Failed -join '; ')" }
    if (-not (Test-Path -LiteralPath $active -PathType Container)) { throw 'The active MagDrum bundle was moved.' }
    if ((Test-Path -LiteralPath $oldSystem -PathType Container) -or (Test-Path -LiteralPath $oldUser -PathType Container)) {
        throw 'An old MagDrum bundle remains in a default VST3 scan root.'
    }
    if (-not (Test-Path -LiteralPath $otherVendor -PathType Container)) { throw 'Cleanup moved a different vendor plugin.' }
    if (@(Get-ChildItem -LiteralPath $backupParent -Directory -Filter 'MagDrum*.vst3.backup-*').Count -ne 2) {
        throw 'Old MagDrum bundles were not preserved in the backup directory.'
    }
}
finally {
    if (Test-Path -LiteralPath $testRoot) { Remove-Item -LiteralPath $testRoot -Recurse -Force }
}

Write-Host 'Windows installer cleanup passed: duplicate MagDrum bundles are backed up outside default VST3 scan roots.'
