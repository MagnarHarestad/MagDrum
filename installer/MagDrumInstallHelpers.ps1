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
