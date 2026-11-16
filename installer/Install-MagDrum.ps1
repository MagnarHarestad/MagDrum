[CmdletBinding(SupportsShouldProcess = $true, ConfirmImpact = 'Medium')]
param(
    [switch] $AllUsers,
    [switch] $Force
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'MagDrumInstallHelpers.ps1')
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
    throw "Close Cubase completely before installing MagDrum. Detected running process(es): $running. A Cubase process may have no visible window."
}
$source = Join-Path $PSScriptRoot 'MagDrum.vst3'
if (-not (Test-Path $source)) { throw 'MagDrum.vst3 was not found beside this installer.' }
$pluginPrincipal = [Security.Principal.WindowsPrincipal] [Security.Principal.WindowsIdentity]::GetCurrent()
$systemWideDestination = Join-Path $env:CommonProgramFiles 'VST3\MagDrum.vst3'
if ($AllUsers) {
    if (-not $pluginPrincipal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw 'Run PowerShell as administrator to install for all users, or omit -AllUsers to install for the current user.'
    }
    $destination = Join-Path $env:CommonProgramFiles 'VST3\MagDrum.vst3'
}
else {
    if (Test-Path -LiteralPath $systemWideDestination) {
        throw "A system-wide MagDrum VST3 already exists at '$systemWideDestination'. To replace it without leaving a duplicate, run PowerShell as administrator and use Install-MagDrum.ps1 -AllUsers."
    }
    $destination = Join-Path $env:LOCALAPPDATA 'Programs\Common\VST3\MagDrum.vst3'
}
$staging = "$destination.installing-$([guid]::NewGuid().ToString('N'))"
$parent = Split-Path $destination
$backupParent = if ($AllUsers) {
    Join-Path $env:ProgramData 'MagDrum\VST3-backups'
}
else {
    Join-Path $env:LOCALAPPDATA 'MagDrum\VST3-backups'
}
$backup = Join-Path $backupParent "MagDrum.vst3.backup-$([DateTime]::Now.ToString('yyyyMMdd-HHmmss'))-$([guid]::NewGuid().ToString('N'))"
if (-not $PSCmdlet.ShouldProcess($destination, 'Install MagDrum VST3')) { return }
if ((Test-Path -LiteralPath $destination) -and -not $Force -and -not $PSCmdlet.ShouldContinue("A MagDrum VST3 already exists at $destination. Keep a backup and replace it?", 'Existing plug-in')) { return }
$pluginModule = Get-ChildItem -LiteralPath $source -Recurse -File -Filter 'MagDrum.vst3' |
    Select-Object -First 1
if (-not $pluginModule) { throw 'The VST3 module binary was not found inside the package.' }
$runtimeArchitecture = if ($pluginModule.Directory.Name -eq 'arm64-win') { 'arm64' } else { 'x64' }
$runtimeInstaller = Join-Path $PSScriptRoot "vc_redist.$runtimeArchitecture.exe"
if (-not (Test-Path -LiteralPath $runtimeInstaller -PathType Leaf)) {
    throw "The required Visual C++ $runtimeArchitecture redistributable is missing from the package."
}
$runtimeVersion = [version](Get-Item -LiteralPath $runtimeInstaller).VersionInfo.FileVersion
$runtimeState = Get-MagDrumVCRuntimeState -Architecture $runtimeArchitecture
if (-not (Test-MagDrumVCRuntimeInstalled -MinimumVersion $runtimeVersion -RuntimeState $runtimeState)) {
    Write-Host "Installing the Microsoft Visual C++ $runtimeArchitecture runtime required by this build."
    $runtimeArguments = @('/install', '/passive', '/norestart')
    $runtimeStart = @{
        FilePath = $runtimeInstaller
        ArgumentList = $runtimeArguments
        Wait = $true
        PassThru = $true
        ErrorAction = 'Stop'
    }
    if (-not $pluginPrincipal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        $runtimeStart.Verb = 'RunAs'
    }
    $runtimeProcess = Start-Process @runtimeStart
    if ($runtimeProcess.ExitCode -eq 3010) {
        Write-Warning 'The Visual C++ runtime was installed; restart Windows before loading MagDrum.'
    }
    elseif ($runtimeProcess.ExitCode -ne 0) {
        throw "Visual C++ runtime installation failed with exit code $($runtimeProcess.ExitCode)."
    }
    elseif (-not (Test-MagDrumVCRuntimeInstalled -MinimumVersion $runtimeVersion -RuntimeState (Get-MagDrumVCRuntimeState -Architecture $runtimeArchitecture))) {
        throw 'The Visual C++ runtime installer completed but the required runtime version is still unavailable.'
    }
}
New-Item -ItemType Directory -Force $parent | Out-Null
New-Item -ItemType Directory -Force $backupParent | Out-Null
try {
    Copy-Item -LiteralPath $source -Destination $staging -Recurse
    # Older installer runs may have left backups inside the scanner path. Move them out first.
    Get-ChildItem -LiteralPath $parent -Directory -Filter 'MagDrum.vst3.backup-*' -ErrorAction SilentlyContinue |
        ForEach-Object { Move-Item -LiteralPath $_.FullName -Destination $backupParent }
    if (Test-Path -LiteralPath $destination) { Move-Item -LiteralPath $destination -Destination $backup }
    Move-Item -LiteralPath $staging -Destination $destination
}
catch {
    if ((Test-Path -LiteralPath $backup) -and -not (Test-Path -LiteralPath $destination)) {
        Move-Item -LiteralPath $backup -Destination $destination
    }
    throw
}
Write-Host "Installed MagDrum VST3 to $destination"
if (Test-Path -LiteralPath $backup) { Write-Host "Previous version preserved at $backup" }
$cleanup = Move-OldMagDrumVst3Bundles -Destination $destination -BackupParent $backupParent -ScanRoots (Get-MagDrumDefaultVst3ScanRoots)
foreach ($movedBundle in $cleanup.Moved) { Write-Host "Moved duplicate MagDrum plug-in outside VST3 scan paths: $movedBundle" }
foreach ($failedBundle in $cleanup.Failed) { Write-Warning "Could not move an older MagDrum plug-in out of a VST3 scan path: $failedBundle" }
Write-Host 'Restart Cubase and run Studio > VST Plug-in Manager > Rescan All.'
