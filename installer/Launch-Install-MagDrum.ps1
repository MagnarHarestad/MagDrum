$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$installer = Join-Path $PSScriptRoot 'Install-MagDrum.ps1'
if (-not (Test-Path -LiteralPath $installer -PathType Leaf)) {
    throw 'Install-MagDrum.ps1 was not found beside this launcher.'
}

$powershell = Join-Path $env:WINDIR 'System32/WindowsPowerShell/v1.0/powershell.exe'
if (-not (Test-Path -LiteralPath $powershell -PathType Leaf)) {
    throw "Windows PowerShell was not found: $powershell"
}
$installerArguments = @(
    '-NoProfile',
    '-ExecutionPolicy', 'Bypass',
    '-File', ('"{0}"' -f $installer),
    '-AllUsers'
)

try {
    Write-Host 'Requesting administrator access through the standard Windows UAC prompt...'
    $process = Start-Process -FilePath $powershell -ArgumentList $installerArguments `
        -Verb RunAs -Wait -PassThru -ErrorAction Stop
    if ($process.ExitCode -ne 0) {
        throw "The MagDrum installer failed with exit code $($process.ExitCode)."
    }
}
catch {
    Write-Error "MagDrum was not installed. $($_.Exception.Message)" -ErrorAction Continue
    exit 1
}
