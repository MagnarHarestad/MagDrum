$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

. (Join-Path $PSScriptRoot 'MagDrumInstallHelpers.ps1')

$pluginPrincipal = [Security.Principal.WindowsPrincipal] [Security.Principal.WindowsIdentity]::GetCurrent()
if (-not $pluginPrincipal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'This one-time cleanup helper must be started through the normal Windows UAC prompt.'
}

$cubaseProcesses = @(Get-Process -Name 'Cubase15', 'Cubase' -ErrorAction SilentlyContinue | Where-Object {
    try {
        $_.Refresh()
        -not $_.HasExited
    }
    catch {
        $false
    }
})
if ($cubaseProcesses.Count -gt 0) {
    $running = ($cubaseProcesses | ForEach-Object { "PID $($_.Id) $($_.ProcessName)" }) -join ', '
    throw "Close Cubase completely before cleaning older MagDrum bundles. Detected running process(es): $running."
}

$userDestination = Join-Path $env:LOCALAPPDATA 'Programs\Common\VST3\MagDrum.vst3'
$backupParent = Join-Path $env:ProgramData 'MagDrum\VST3-backups'
$result = Move-OldMagDrumVst3Bundles -Destination $userDestination -BackupParent $backupParent `
    -ScanRoots (Get-MagDrumDefaultVst3ScanRoots)
foreach ($bundle in $result.Moved) {
    Write-Host "Moved older MagDrum bundle outside VST3 scan paths: $bundle"
}
foreach ($failure in $result.Failed) {
    Write-Warning "Could not move an older MagDrum bundle: $failure"
}
if ($result.Failed.Count -gt 0) { exit 2 }
if ($result.Moved.Count -eq 0) { Write-Host 'No older MagDrum bundles needed cleanup.' }
exit 0
