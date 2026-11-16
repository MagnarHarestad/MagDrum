function Get-MagDrumVCRuntimeState {
    [CmdletBinding()]
    param(
        [ValidateSet('x64', 'arm64')]
        [string] $Architecture = 'x64'
    )

    $subKeyPath = "SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\$Architecture"
    $baseKey = [Microsoft.Win32.RegistryKey]::OpenBaseKey(
        [Microsoft.Win32.RegistryHive]::LocalMachine,
        [Microsoft.Win32.RegistryView]::Registry64)
    try {
        $runtimeKey = $baseKey.OpenSubKey($subKeyPath)
        if ($null -eq $runtimeKey) { return $null }
        try {
            [pscustomobject]@{
                Installed = [int]$runtimeKey.GetValue('Installed', 0)
                Version = [string]$runtimeKey.GetValue('Version', '')
            }
        }
        finally { $runtimeKey.Dispose() }
    }
    finally { $baseKey.Dispose() }
}

function Test-MagDrumVCRuntimeInstalled {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [version] $MinimumVersion,
        [Parameter(Mandatory)] [AllowNull()] [object] $RuntimeState
    )

    if ($null -eq $RuntimeState -or [int]$RuntimeState.Installed -ne 1) { return $false }
    try {
        $installedVersion = [version](([string]$RuntimeState.Version).TrimStart([char]'v'))
        return $installedVersion -ge $MinimumVersion
    }
    catch {
        return $false
    }
}

function Get-MagDrumDefaultVst3ScanRoots {
    [CmdletBinding()]
    param(
        [string] $ProgramFilesPath = $env:ProgramFiles,
        [string] $LocalAppDataPath = $env:LOCALAPPDATA
    )

    $roots = @(
        (Join-Path $ProgramFilesPath 'Common Files\VST3'),
        (Join-Path $LocalAppDataPath 'Programs\Common\VST3')
    )
    $seen = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
    foreach ($root in $roots) {
        if ($seen.Add([System.IO.Path]::GetFullPath($root))) {
            $root
        }
    }
}

function Move-OldMagDrumVst3Bundles {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $Destination,
        [Parameter(Mandatory)] [string] $BackupParent,
        [Parameter(Mandatory)] [string[]] $ScanRoots
    )

    $destinationPath = [System.IO.Path]::GetFullPath($Destination).TrimEnd([System.IO.Path]::DirectorySeparatorChar)
    $searchRoots = @($ScanRoots) + (Split-Path -Parent $destinationPath)
    $seenRoots = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
    $moved = [System.Collections.Generic.List[string]]::new()
    $failed = [System.Collections.Generic.List[string]]::new()
    New-Item -ItemType Directory -Path $BackupParent -Force | Out-Null

    foreach ($root in $searchRoots) {
        if ([string]::IsNullOrWhiteSpace($root)) { continue }
        $rootPath = [System.IO.Path]::GetFullPath($root)
        if (-not $seenRoots.Add($rootPath) -or -not (Test-Path -LiteralPath $rootPath -PathType Container)) { continue }

        foreach ($bundle in (Get-ChildItem -LiteralPath $rootPath -Directory -Filter 'MagDrum*.vst3' -ErrorAction SilentlyContinue)) {
            $bundlePath = [System.IO.Path]::GetFullPath($bundle.FullName).TrimEnd([System.IO.Path]::DirectorySeparatorChar)
            if ([System.StringComparer]::OrdinalIgnoreCase.Equals($bundlePath, $destinationPath)) { continue }

            $backupName = "$($bundle.Name).backup-$([DateTime]::Now.ToString('yyyyMMdd-HHmmss'))-$([guid]::NewGuid().ToString('N'))"
            $backupPath = Join-Path $BackupParent $backupName
            try {
                Move-Item -LiteralPath $bundlePath -Destination $backupPath -ErrorAction Stop
                $moved.Add($backupPath)
            }
            catch {
                $failed.Add("$bundlePath -> $backupPath ($($_.Exception.Message))")
            }
        }
    }

    [pscustomobject]@{
        Moved = $moved.ToArray()
        Failed = $failed.ToArray()
    }
}
