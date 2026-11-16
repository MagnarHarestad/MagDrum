[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string] $PackagePath,
    [ValidateRange(0, 99999)]
    [int] $BuildRevision
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$resolvedPackage = (Resolve-Path -LiteralPath $PackagePath).Path
Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive = [System.IO.Compression.ZipFile]::OpenRead($resolvedPackage)
$temporaryDirectory = Join-Path $env:TEMP ('MagDrum-Package-Check-' + [guid]::NewGuid().ToString('N'))

try {
    $entries = @{}
    foreach ($entry in $archive.Entries) {
        $entries[$entry.FullName.Replace('\', '/')] = $entry
    }

    $requiredEntries = @(
        'MagDrum.exe',
        'MagDrum.vst3/Contents/x86_64-win/MagDrum.vst3',
        'MagDrum.vst3/Contents/x86_64-win/MagDrum.exe',
        'Install-MagDrum.ps1',
        'MagDrumInstallHelpers.ps1',
        'README.txt',
        'LICENSE',
        'THIRD-PARTY-NOTICES.md',
        'JUCE-LICENSE.md',
        'VST3-SDK-LICENSE.txt',
        'FLAC-LICENSE.txt',
        'Ogg-Vorbis-LICENSE.txt',
        'libvorbis-COPYING.txt',
        'JPEG-LICENSE.txt',
        'PNG-LICENSE.txt',
        'zlib-LICENSE.txt',
        'HarfBuzz-LICENSE.txt',
        'SheenBidi-LICENSE.txt',
        'vc_redist.x64.exe'
    )
    $missingEntries = @($requiredEntries | Where-Object { -not $entries.ContainsKey($_) })
    if ($missingEntries.Count -gt 0) {
        throw "Windows package is missing required files: $($missingEntries -join ', ')."
    }

    New-Item -ItemType Directory -Path $temporaryDirectory | Out-Null
    $expectedVersion = "0.1.2.$BuildRevision"
    foreach ($entryName in @(
        'MagDrum.exe',
        'MagDrum.vst3/Contents/x86_64-win/MagDrum.vst3'
    )) {
        $destination = Join-Path $temporaryDirectory ([IO.Path]::GetFileName($entryName))
        $inputStream = $entries[$entryName].Open()
        try {
            $outputStream = [IO.File]::Create($destination)
            try { $inputStream.CopyTo($outputStream) }
            finally { $outputStream.Dispose() }
        }
        finally { $inputStream.Dispose() }

        $actualVersion = [string](Get-Item -LiteralPath $destination).VersionInfo.ProductVersion
        if ($actualVersion -ne $expectedVersion) {
            throw "Packaged '$entryName' reports version '$actualVersion'; expected '$expectedVersion'."
        }
    }

    $runtimePath = Join-Path $temporaryDirectory 'vc_redist.x64.exe'
    $runtimeStream = $entries['vc_redist.x64.exe'].Open()
    try {
        $runtimeFile = [IO.File]::Create($runtimePath)
        try { $runtimeStream.CopyTo($runtimeFile) }
        finally { $runtimeFile.Dispose() }
    }
    finally { $runtimeStream.Dispose() }

    $signature = Get-AuthenticodeSignature -FilePath $runtimePath
    if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notlike '*CN=Microsoft Corporation*') {
        throw 'The packaged Microsoft Visual C++ redistributable does not have a valid Microsoft signature.'
    }

    Write-Host "Windows package contents, license notices, signatures, and version $expectedVersion passed."
}
finally {
    $archive.Dispose()
    if (Test-Path -LiteralPath $temporaryDirectory) {
        Remove-Item -LiteralPath $temporaryDirectory -Recurse -Force
    }
}
