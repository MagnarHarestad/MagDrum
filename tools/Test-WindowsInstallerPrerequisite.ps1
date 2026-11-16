$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

. (Join-Path $PSScriptRoot '../installer/MagDrumInstallHelpers.ps1')

$requiredVersion = [version]'14.51.36247.0'
$cases = @(
    @{ Name = 'matching version'; State = [pscustomobject]@{ Installed = 1; Version = 'v14.51.36247.00' }; Expected = $true },
    @{ Name = 'newer version'; State = [pscustomobject]@{ Installed = 1; Version = 'v14.52.100.0' }; Expected = $true },
    @{ Name = 'older version'; State = [pscustomobject]@{ Installed = 1; Version = 'v14.50.999.0' }; Expected = $false },
    @{ Name = 'not installed'; State = [pscustomobject]@{ Installed = 0; Version = 'v14.51.36247.00' }; Expected = $false },
    @{ Name = 'malformed version'; State = [pscustomobject]@{ Installed = 1; Version = 'unknown' }; Expected = $false }
)

foreach ($case in $cases) {
    $actual = Test-MagDrumVCRuntimeInstalled -MinimumVersion $requiredVersion -RuntimeState $case.State
    if ($actual -ne $case.Expected) {
        throw "Visual C++ runtime check failed for '$($case.Name)': expected $($case.Expected), got $actual."
    }
}

$installerPath = Join-Path $PSScriptRoot '../installer/Install-MagDrum.ps1'
$installer = [System.IO.File]::ReadAllText($installerPath)
foreach ($requiredText in @('Get-MagDrumVCRuntimeState', 'Test-MagDrumVCRuntimeInstalled', 'vc_redist.', '/install', 'RunAs')) {
    if ($installer -notmatch [regex]::Escape($requiredText)) {
        throw "Windows installer is missing Visual C++ prerequisite handling: $requiredText"
    }
}

Write-Host 'Windows installer prerequisite checks passed: missing or outdated Visual C++ runtimes trigger the bundled installer.'
