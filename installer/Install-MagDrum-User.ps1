[CmdletBinding()]
param(
    [switch] $SkipSystemCleanup
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

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
    throw "Close Cubase completely before installing MagDrum. Detected running process(es): $running."
}

$source = Join-Path $PSScriptRoot 'MagDrum.vst3'
if (-not (Test-Path -LiteralPath $source -PathType Container)) {
    throw 'MagDrum.vst3 was not found beside this installer.'
}
$module = Get-ChildItem -LiteralPath $source -Recurse -File -Filter 'MagDrum.vst3' |
    Select-Object -First 1
if (-not $module) { throw 'The VST3 module binary was not found inside the package.' }
$architecture = if ($module.Directory.Name -eq 'arm64-win') { 'arm64' } else { 'x64' }

$runtimeInstaller = Join-Path $PSScriptRoot "vc_redist.$architecture.exe"
if (-not (Test-Path -LiteralPath $runtimeInstaller -PathType Leaf)) {
    throw "The required Visual C++ $architecture redistributable is missing from the package."
}
$minimumRuntime = [version](Get-Item -LiteralPath $runtimeInstaller).VersionInfo.FileVersion
$runtimeState = Get-MagDrumVCRuntimeState -Architecture $architecture
if (-not (Test-MagDrumVCRuntimeInstalled -MinimumVersion $minimumRuntime -RuntimeState $runtimeState)) {
    throw "The no-UAC installer requires Microsoft Visual C++ $architecture runtime $minimumRuntime or newer. Ask an administrator to install the runtime once, then run this installer again. No system changes were made."
}

$userVst3Root = Join-Path $env:LOCALAPPDATA 'Programs\Common\VST3'
$destination = Join-Path $userVst3Root 'MagDrum.vst3'
$systemVst3Root = Join-Path $env:CommonProgramFiles 'VST3'
$systemBundle = Join-Path $systemVst3Root 'MagDrum.vst3'
$staging = "$destination.installing-$([guid]::NewGuid().ToString('N'))"
$backupParent = Join-Path $env:LOCALAPPDATA 'MagDrum\VST3-backups'
$backup = Join-Path $backupParent "MagDrum.vst3.backup-$([DateTime]::Now.ToString('yyyyMMdd-HHmmss'))-$([guid]::NewGuid().ToString('N'))"
$parent = Split-Path $destination

New-Item -ItemType Directory -Force $parent | Out-Null
New-Item -ItemType Directory -Force $backupParent | Out-Null
try {
    Copy-Item -LiteralPath $source -Destination $staging -Recurse
    if (Test-Path -LiteralPath $destination) { Move-Item -LiteralPath $destination -Destination $backup }
    Move-Item -LiteralPath $staging -Destination $destination
}
catch {
    if ((Test-Path -LiteralPath $backup) -and -not (Test-Path -LiteralPath $destination)) {
        Move-Item -LiteralPath $backup -Destination $destination
    }
    if (Test-Path -LiteralPath $staging) { Remove-Item -LiteralPath $staging -Recurse -Force }
    throw
}

$installedModule = Get-ChildItem -LiteralPath $destination -Recurse -File -Filter 'MagDrum.vst3' |
    Select-Object -First 1
if (-not $installedModule) { throw 'The user-level VST3 was copied, but its module binary is missing.' }
$version = [string](Get-Item -LiteralPath $installedModule.FullName).VersionInfo.ProductVersion
Write-Host "Installed MagDrum VST3 $version for this Windows user at: $destination"
if (Test-Path -LiteralPath $backup) { Write-Host "Previous user-level bundle preserved at: $backup" }

if (Test-Path -LiteralPath $systemBundle -PathType Container) {
    if ($SkipSystemCleanup) {
        Write-Warning "The system-wide MagDrum copy remains at '$systemBundle'. VST3 hosts should prefer the user-level copy; verify the version in Cubase."
    }
    else {
        $cleanupScript = Join-Path $PSScriptRoot 'Remove-OldMagDrumBundles.ps1'
        if (-not (Test-Path -LiteralPath $cleanupScript -PathType Leaf)) {
            Write-Warning "The user-level copy is installed, but the one-time system cleanup helper is missing. The old system bundle remains at '$systemBundle'."
        }
        else {
            $powershell = Join-Path $env:WINDIR 'System32/WindowsPowerShell/v1.0/powershell.exe'
            $arguments = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', ('"{0}"' -f $cleanupScript))
            try {
                Write-Host 'Requesting one-time administrator approval to move older MagDrum bundles out of VST3 scan folders...'
                $process = Start-Process -FilePath $powershell -ArgumentList $arguments -Verb RunAs -Wait -PassThru -ErrorAction Stop
                if ($process.ExitCode -ne 0) {
                    Write-Warning "The per-user version is installed, but system cleanup returned exit code $($process.ExitCode). The old system bundle remains."
                }
            }
            catch {
                Write-Warning "The per-user version is installed, but system cleanup was cancelled or failed. The old system bundle remains at '$systemBundle'."
            }
        }
    }
}
else {
    Write-Host 'No system-wide MagDrum bundle was found; future per-user updates will not need UAC.'
}

Write-Host 'Restart Cubase and choose Studio > VST Plug-in Manager > Rescan All. Confirm the reported version is the one just installed.'
