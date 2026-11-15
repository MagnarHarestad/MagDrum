$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$installerPath = Join-Path $PSScriptRoot '../installer/Install-MagDrum.ps1'
$tokens = $null
$parseErrors = $null
$installerAst = [System.Management.Automation.Language.Parser]::ParseFile(
    $installerPath,
    [ref] $tokens,
    [ref] $parseErrors)
if ($parseErrors.Count -gt 0) {
    throw "Windows installer syntax is invalid: $($parseErrors -join '; ')"
}

$processFilterCommand = $installerAst.Find({
    param ($node)
    if ($node -isnot [System.Management.Automation.Language.CommandAst]) { return $false }
    if ($node.GetCommandName() -ne 'Where-Object') { return $false }
    return $node.Extent.Text -match '\.HasExited'
}, $true)
if ($null -eq $processFilterCommand) {
    throw 'Installer process guard must refresh and filter exited Cubase process records.'
}

$filterAst = $processFilterCommand.CommandElements |
    Where-Object { $_ -is [System.Management.Automation.Language.ScriptBlockExpressionAst] } |
    Select-Object -First 1
if ($null -eq $filterAst) {
    throw 'Installer process guard has no Where-Object predicate.'
}
$predicateText = ($filterAst.ScriptBlock.EndBlock.Statements |
    ForEach-Object { $_.Extent.Text }) -join [Environment]::NewLine
$processPredicate = [scriptblock]::Create($predicateText)

$liveProcess = Get-Process -Id $PID -ErrorAction Stop
$liveResult = @($liveProcess | Where-Object -FilterScript $processPredicate)
if ($liveResult.Count -ne 1) {
    throw 'Installer process guard failed to detect a live process.'
}

$processStartInfo = [System.Diagnostics.ProcessStartInfo]::new()
$processStartInfo.FileName = $env:ComSpec
$processStartInfo.Arguments = '/d /c exit 0'
$processStartInfo.CreateNoWindow = $true
$processStartInfo.UseShellExecute = $false
$finishedProcess = [System.Diagnostics.Process]::Start($processStartInfo)
if ($null -eq $finishedProcess) {
    throw 'Could not create an exited-process fixture for installer guard validation.'
}
try {
    $finishedProcess.WaitForExit()
    $finishedResult = @($finishedProcess | Where-Object -FilterScript $processPredicate)
    if ($finishedResult.Count -ne 0) {
        throw 'Installer process guard treated an exited process record as live.'
    }
}
finally {
    $finishedProcess.Dispose()
}

Write-Host 'Windows installer process guard passed: live processes block; exited entries are ignored.'
