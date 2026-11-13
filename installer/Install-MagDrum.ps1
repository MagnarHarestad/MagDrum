[CmdletBinding(SupportsShouldProcess = $true, ConfirmImpact = 'Medium')]
param(
    [switch] $AllUsers
)

$ErrorActionPreference = 'Stop'
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
        Write-Warning "A system-wide MagDrum VST3 already exists at '$systemWideDestination'. A per-user install creates a second copy. To replace the system-wide copy and keep a backup, run PowerShell as administrator and use Install-MagDrum.ps1 -AllUsers."
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
if ((Test-Path -LiteralPath $destination) -and -not $PSCmdlet.ShouldContinue("A MagDrum VST3 already exists at $destination. Keep a backup and replace it?", 'Existing plug-in')) { return }
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
Write-Host 'Restart Cubase and run Studio > VST Plug-in Manager > Rescan All.'
