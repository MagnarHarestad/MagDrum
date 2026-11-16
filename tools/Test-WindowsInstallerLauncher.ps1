$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$launcherPath = Join-Path $PSScriptRoot '../installer/Launch-Install-MagDrum.ps1'
$cmdPath = Join-Path $PSScriptRoot '../installer/Install-MagDrum.cmd'
$userInstallerPath = Join-Path $PSScriptRoot '../installer/Install-MagDrum-User.ps1'
$userCmdPath = Join-Path $PSScriptRoot '../installer/Install-MagDrum-User.cmd'
$tokens = $null
$parseErrors = $null
$launcherAst = [System.Management.Automation.Language.Parser]::ParseFile(
    $launcherPath,
    [ref] $tokens,
    [ref] $parseErrors)
if ($parseErrors.Count -gt 0) {
    throw "Windows installer launcher syntax is invalid: $($parseErrors -join '; ')"
}

$startProcess = $launcherAst.Find({
    param ($node)
    $node -is [System.Management.Automation.Language.CommandAst] -and
        $node.GetCommandName() -eq 'Start-Process'
}, $true)
if ($null -eq $startProcess -or $startProcess.Extent.Text -notmatch '(?i)-Verb\s+RunAs' -or
    $startProcess.Extent.Text -notmatch '(?i)-Wait' -or
    $startProcess.Extent.Text -notmatch '(?i)-PassThru') {
    throw 'The launcher must use standard UAC elevation, wait for the installer, and inspect its exit code.'
}

$installerCall = $launcherAst.Find({
    param ($node)
    $node -is [System.Management.Automation.Language.CommandAst] -and
        $node.GetCommandName() -eq 'Join-Path' -and
        $node.Extent.Text -match 'Install-MagDrum\.ps1'
}, $true)
if ($null -eq $installerCall -or
    -not (Get-Content -LiteralPath $launcherPath -Raw).Contains("'-AllUsers'")) {
    throw 'The launcher must invoke the packaged all-users installer.'
}
if ((Get-Content -LiteralPath $launcherPath -Raw) -match '(?i)[''"" ]-Force\b') {
    throw 'The launcher must leave the existing-install confirmation enabled.'
}

$launcherText = Get-Content -LiteralPath $launcherPath -Raw
if (-not $launcherText.Contains('Join-Path $env:WINDIR ''System32/WindowsPowerShell/v1.0/powershell.exe''')) {
    throw 'The launcher must use the Windows PowerShell executable, not assume PSHOME contains powershell.exe.'
}

$cmd = Get-Content -LiteralPath $cmdPath -Raw
if ($cmd -notmatch 'Launch-Install-MagDrum\.ps1' -or $cmd -notmatch '(?im)^\s*pause\s*$') {
    throw 'The double-click command file must start the UAC launcher and keep failures visible.'
}

$userTokens = $null
$userParseErrors = $null
$userInstallerAst = [System.Management.Automation.Language.Parser]::ParseFile(
    $userInstallerPath,
    [ref] $userTokens,
    [ref] $userParseErrors)
if ($userParseErrors.Count -gt 0) {
    throw "Per-user installer syntax is invalid: $($userParseErrors -join '; ')"
}
$userInstallerText = Get-Content -LiteralPath $userInstallerPath -Raw
if ($userInstallerText -match '(?i)Launch-Install-MagDrum\.ps1|Install-MagDrum\.ps1') {
    throw 'The per-user installer must not invoke the system-wide installer.'
}
if ($userInstallerText -notmatch 'LOCALAPPDATA.{0,80}Programs\\Common\\VST3') {
    throw 'The per-user installer must write its VST3 bundle under LOCALAPPDATA.'
}
$userStartCalls = @($userInstallerAst.FindAll({
    param ($node)
    $node -is [System.Management.Automation.Language.CommandAst] -and
        $node.GetCommandName() -eq 'Start-Process'
}, $true))
if ($userStartCalls.Count -ne 1 -or $userStartCalls[0].Extent.Text -notmatch '(?i)-Verb\s+RunAs') {
    throw 'The per-user flow may elevate exactly one child process for one-time legacy cleanup.'
}
if ($userInstallerText -notmatch 'Remove-OldMagDrumBundles\.ps1' -or
    $userInstallerText -notmatch 'SkipSystemCleanup') {
    throw 'Legacy cleanup must be explicit and optional so later per-user updates run without UAC.'
}
$userCmd = Get-Content -LiteralPath $userCmdPath -Raw
if ($userCmd -notmatch 'Install-MagDrum-User\.ps1' -or $userCmd -match '(?i)runas|Start-Process') {
    throw 'The no-UAC command file must invoke the per-user installer.'
}

Write-Host 'Windows installer checks passed: system installs use UAC; per-user updates avoid UAC and isolate the one-time legacy cleanup.'
