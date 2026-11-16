$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$launcherPath = Join-Path $PSScriptRoot '../installer/Launch-Install-MagDrum.ps1'
$cmdPath = Join-Path $PSScriptRoot '../installer/Install-MagDrum.cmd'
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

$cmd = Get-Content -LiteralPath $cmdPath -Raw
if ($cmd -notmatch 'Launch-Install-MagDrum\.ps1' -or $cmd -notmatch '(?im)^\s*pause\s*$') {
    throw 'The double-click command file must start the UAC launcher and keep failures visible.'
}

Write-Host 'Windows installer launcher passed: UAC is explicit, replacement confirmation remains, and failures stay visible.'
