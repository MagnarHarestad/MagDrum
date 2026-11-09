param(
    [Parameter(Mandatory = $true)]
    [string] $SourceRoot,

    [Parameter(Mandatory = $true)]
    [string] $OutputRoot
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$source = (Resolve-Path -LiteralPath $SourceRoot).Path
$samplesRoot = Join-Path $source 'Samples'
if (-not (Test-Path -LiteralPath $samplesRoot -PathType Container)) {
    throw "The source root must contain a Samples directory: $source"
}

$output = [System.IO.Path]::GetFullPath($OutputRoot)
if (Test-Path -LiteralPath $output) {
    throw "Output directory already exists; choose a new path: $output"
}

$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path.TrimEnd([System.IO.Path]::DirectorySeparatorChar)
$repositoryPrefix = $repositoryRoot + [System.IO.Path]::DirectorySeparatorChar
if ($output.Equals($repositoryRoot, [System.StringComparison]::OrdinalIgnoreCase) -or
    $output.StartsWith($repositoryPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw 'Evaluation audio must stay outside the MagDrum repository.'
}
$sourcePrefix = $source.TrimEnd([System.IO.Path]::DirectorySeparatorChar) + [System.IO.Path]::DirectorySeparatorChar
if ($output.Equals($source, [System.StringComparison]::OrdinalIgnoreCase) -or
    $output.StartsWith($sourcePrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw 'Output must not be created inside the downloaded source library.'
}

function Get-SelectedFiles([string] $RelativeDirectory, [string] $Filter, [int] $Count, [switch] $Recurse) {
    $directory = Join-Path $samplesRoot $RelativeDirectory
    if (-not (Test-Path -LiteralPath $directory -PathType Container)) {
        throw "Expected source directory is missing: $directory"
    }

    $files = @(Get-ChildItem -LiteralPath $directory -File -Filter $Filter -Recurse:$Recurse | Sort-Object FullName | Select-Object -First $Count)
    if ($files.Count -ne $Count) {
        throw "Expected $Count files matching '$Filter' under '$directory', found $($files.Count)."
    }
    return $files
}

function Add-Category([string] $Category, [System.IO.FileInfo[]] $Files) {
    $destination = Join-Path $output $Category
    [void](New-Item -ItemType Directory -Path $destination -Force)
    $index = 0
    foreach ($file in $Files) {
        ++$index
        $name = '{0:D3}_{1}' -f $index, $file.Name
        Copy-Item -LiteralPath $file.FullName -Destination (Join-Path $destination $name)
    }
    Write-Host ("{0}: {1}" -f $Category, $index)
}

$groups = @(
    @{ Category = 'kicks'; Files = @(Get-SelectedFiles 'kickmic\kick' '*.flac' 14) },
    @{ Category = 'snares'; Files = @(Get-SelectedFiles 'snaremic\snare' '*.flac' 14) },
    @{ Category = 'closed-hats'; Files = @(Get-SelectedFiles 'kickmic\hh' '*_hh_closed_*' 14) },
    @{ Category = 'open-hats'; Files = @(Get-SelectedFiles 'kickmic\hh' '*_hh_open_*' 12) },
    @{ Category = 'pedal-hats'; Files = @(Get-SelectedFiles 'kickmic\hh' '*_hh_pedal_*' 12) },
    @{ Category = 'toms'; Files = @((Get-SelectedFiles 'kickmic\htom' '*.flac' 7) + (Get-SelectedFiles 'kickmic\ltom' '*.flac' 7)) },
    @{ Category = 'rides'; Files = @((Get-SelectedFiles 'kickmic\ride' '*.flac' 7) + (Get-SelectedFiles 'kickmic\flatride' '*_flatride_ride_*' 7)) },
    @{ Category = 'crashes'; Files = @((Get-SelectedFiles 'kickmic\crash' '*.flac' 7) + (Get-SelectedFiles 'kickmic\flatride' '*_flatride_crash_*' 4)) },
    @{ Category = 'percs'; Files = @(Get-SelectedFiles 'perc' '*.wav' 16 -Recurse) }
)

[void](New-Item -ItemType Directory -Path $output)
foreach ($group in $groups) {
    Add-Category $group.Category $group.Files
}

Write-Host "Prepared $((Get-ChildItem -LiteralPath $output -Recurse -File | Measure-Object).Count) files in $output"
Write-Host 'The source library was only read; evaluation audio is outside the repository.'
