[CmdletBinding(SupportsShouldProcess = $true, ConfirmImpact = 'None')]
param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^v[0-9]+\.[0-9]+\.[0-9]+-test\.[0-9]+$')]
    [string] $Tag,

    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string[]] $Asset
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = (Resolve-Path $PSScriptRoot).Path
$publishDirectory = Join-Path $repoRoot 'latest-publish'
$knownAssets = @(
    'MagDrum-Windows-x64.zip',
    'MagDrum-Windows-ARM64.zip',
    'MagDrum-macOS-Intel.zip',
    'MagDrum-macOS-AppleSilicon.zip',
    'MagDrum-Linux-x64.tar.gz'
)

if (-not (Get-Command gh -ErrorAction SilentlyContinue)) {
    throw 'GitHub CLI (gh) is required. Authenticate with `gh auth login` first.'
}
$previousGhToken = $env:GH_TOKEN
$usingGitCredential = $false
try {
    & gh auth status
    if ($LASTEXITCODE -ne 0) {
        $credentialInput = @('protocol=https', 'host=github.com', 'path=MagnarHarestad/MagDrum', '')
        $credentialLines = $credentialInput | & git credential fill 2>$null
        $credential = @{}
        foreach ($line in $credentialLines) {
            $separator = $line.IndexOf('=')
            if ($separator -gt 0) { $credential[$line.Substring(0, $separator)] = $line.Substring($separator + 1) }
        }
        if (-not $credential.ContainsKey('password') -or [string]::IsNullOrWhiteSpace($credential['password'])) {
            throw 'GitHub CLI authentication failed and Git Credential Manager has no credential for this repository. Run `gh auth login -h github.com`.'
        }
        $env:GH_TOKEN = $credential['password']
        $usingGitCredential = $true
        & gh auth status
        if ($LASTEXITCODE -ne 0) { throw 'GitHub rejected the credential Git Credential Manager used for the repository push.' }
    }

$releaseAssets = [System.Collections.Generic.List[string]]::new()
$checksums = [System.Collections.Generic.List[string]]::new()
$includedNames = [System.Collections.Generic.List[string]]::new()
foreach ($name in $Asset) {
    if ($name -notin $knownAssets) { throw "Unsupported release asset name: $name" }
    $path = Join-Path $publishDirectory $name
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Release asset not found: $path" }
    $resolvedPath = (Resolve-Path -LiteralPath $path).Path
    if (-not $resolvedPath.StartsWith($publishDirectory + [System.IO.Path]::DirectorySeparatorChar, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Release assets must be inside $publishDirectory."
    }
    $hash = (Get-FileHash -LiteralPath $resolvedPath -Algorithm SHA256).Hash.ToLowerInvariant()
    $checksums.Add("$hash  $name")
    $releaseAssets.Add($resolvedPath)
    $includedNames.Add($name)
}
$uniqueNames = @($includedNames | Select-Object -Unique)
if ($includedNames.Count -ne $uniqueNames.Count) {
    throw 'Each release asset may be listed only once.'
}

$checksumName = "MagDrum-$Tag-SHA256SUMS.txt"
$checksumPath = Join-Path $publishDirectory $checksumName
Set-Content -LiteralPath $checksumPath -Value $checksums -Encoding ascii
$releaseAssets.Add($checksumPath)

$missingNames = @($knownAssets | Where-Object { $_ -notin $includedNames })
$noteLines = @(
    "Locally built MagDrum test packages for $Tag.",
    'MagDrum original source code is licensed under GNU AGPLv3. The packages include the license and current third-party notices.',
    'The source repository contains the corresponding source for this release.',
    '',
    'Included packages:',
    ($includedNames | ForEach-Object { "- $_" }),
    '',
    "SHA-256 checksums are attached as $checksumName."
)
if ($missingNames.Count -gt 0) {
    $noteLines += @('', 'Not included in this release:', ($missingNames | ForEach-Object { "- $_" }))
}
if ($includedNames -contains 'MagDrum-Windows-x64.zip' -or $includedNames -contains 'MagDrum-Windows-ARM64.zip') {
    $noteLines += @(
        '',
        'The Windows installer refuses to replace MagDrum while a Cubase process is running, including a hidden process without a visible window.',
        'The sample-sort label and library scan separators use ASCII punctuation to avoid code-page glyph corruption. The eight-language menu remains available.',
        'The installed-library scan includes Cubase 15 Media and VST Sound locations alongside detected Native Instruments and Spectrasonics locations.'
    )
}

if ($includedNames -contains 'MagDrum-Windows-ARM64.zip') {
    $noteLines += @(
        '',
        'Windows ARM64 was cross-compiled on an x64 host and has not been runtime-tested on ARM64 Windows.',
        'JUCE omitted the optional VST3 moduleinfo.json during cross-compilation; hosts may take longer to scan the plugin.'
    )
}
if ($includedNames -contains 'MagDrum-Linux-x64.tar.gz') {
    $noteLines += @(
        '',
        'The Linux archive includes Install-MagDrum-Linux.sh for a per-user app and VST3 install. It places previous plug-in bundles under XDG_DATA_HOME (or ~/.local/share), outside VST3 scan paths.',
        'Linux x64 was built and tested in WSL Ubuntu 24.04. CTest loads the byte-identical release VST3 through JUCE, scans MIDI notes 0-127 and confirms audible note 36. Ardour 8.4 also recognizes the package; interactive Ardour session and physical Linux audio-device tests remain pending.'
    )
}
$notesPath = Join-Path $env:TEMP ("MagDrum-$Tag-release-notes.txt")
Set-Content -LiteralPath $notesPath -Value $noteLines -Encoding utf8

$existingTags = & gh release list --limit 100 --json tagName --jq '.[].tagName'
if ($LASTEXITCODE -ne 0) { throw 'Could not query existing GitHub releases.' }
if ($existingTags -contains $Tag) { throw "Release $Tag already exists." }

if ($PSCmdlet.ShouldProcess("GitHub release $Tag", "Upload $($includedNames.Count) local package(s)")) {
    & gh release create $Tag @releaseAssets --title "MagDrum $Tag" --notes-file $notesPath --prerelease --target main
    if ($LASTEXITCODE -ne 0) { throw "GitHub release creation failed for $Tag." }

    Write-Host "Created prerelease $Tag with $($includedNames.Count) platform package(s)."
    $websitePublisher = Join-Path $repoRoot 'tools/Publish-GitHubPages.ps1'
    if (Test-Path -LiteralPath $websitePublisher -PathType Leaf) {
        try { & $websitePublisher }
        catch { Write-Warning "Release succeeded, but the public website update failed: $_" }
    }
}
}
finally {
    if ($usingGitCredential) {
        if ($null -eq $previousGhToken) { Remove-Item Env:GH_TOKEN -ErrorAction SilentlyContinue }
        else { $env:GH_TOKEN = $previousGhToken }
    }
}
