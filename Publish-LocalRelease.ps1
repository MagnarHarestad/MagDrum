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
    # Resolve-Path honors -WhatIf and returns no value in a dry run; these names are
    # allow-listed, so normalize them without suppressing validation in that mode.
    $resolvedPath = [System.IO.Path]::GetFullPath($path)
    if (-not $resolvedPath.StartsWith($publishDirectory + [System.IO.Path]::DirectorySeparatorChar, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Release assets must be inside $publishDirectory."
    }
    $stream = [System.IO.File]::OpenRead($resolvedPath)
    try {
        $hasher = [System.Security.Cryptography.SHA256]::Create()
        try { $hash = [System.BitConverter]::ToString($hasher.ComputeHash($stream)).Replace('-', '').ToLowerInvariant() }
        finally { $hasher.Dispose() }
    }
    finally { $stream.Dispose() }
    $checksums.Add("$hash  $name")
    $releaseAssets.Add($resolvedPath)
    $includedNames.Add($name)
}

if ($includedNames -contains 'MagDrum-Windows-x64.zip') {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [System.IO.Compression.ZipFile]::OpenRead((Join-Path $publishDirectory 'MagDrum-Windows-x64.zip'))
    try {
        $manifestEntry = $zip.Entries |
            Where-Object { $_.FullName.Replace([string][char]92, '/') -eq 'MagDrum.vst3/Contents/Resources/moduleinfo.json' } |
            Select-Object -First 1
        if ($null -eq $manifestEntry) { throw 'The Windows x64 package has no VST3 moduleinfo.json; refusing to publish an unidentifiable test build.' }
        $manifestReader = [System.IO.StreamReader]::new($manifestEntry.Open())
        try {
            # VST3 moduleinfo.json is JSON-like and may include trailing commas;
            # Windows PowerShell 5.1's ConvertFrom-Json rejects them.
            $manifestJson = $manifestReader.ReadToEnd() -replace ',(?=\s*[}\]])', ''
            $moduleInfo = $manifestJson | ConvertFrom-Json
        }
        finally { $manifestReader.Dispose() }
    }
    finally { $zip.Dispose() }

    if ($Tag -notmatch '^v([0-9]+\.[0-9]+\.[0-9]+)-test\.([0-9]+)$') { throw "Cannot derive the Windows product version from release tag $Tag." }
    $expectedProductVersion = "$($Matches[1]).$($Matches[2])"
    if ($moduleInfo.Version -ne $expectedProductVersion) {
        throw "Windows package VST3 version '$($moduleInfo.Version)' does not match release tag $Tag (expected $expectedProductVersion). Rebuild with -BuildRevision $($Matches[2])."
    }
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
        'The Windows ZIP includes Install-MagDrum.cmd for a double-click system-wide install through standard UAC. It asks before replacing the existing plug-in and preserves the previous bundle outside VST3 scan folders.',
        'The Windows package bundles the Microsoft-signed Visual C++ runtime version used by this build; installation updates the system runtime only when it is missing or older.',
        'The sample-sort label and library scan separators use ASCII punctuation to avoid code-page glyph corruption. Localization catalogs are decoded as UTF-8; a Windows smoke test checks drum-machine, library and sort labels in all eight languages, including French accents.',
        'The sample browser uses a persistent SQLite FTS5 trigram index for substring search over sample names and paths, tags, sources, plugins and kits. Windows and Linux tests cover schema migration rebuilds and a 250,000-row indexed search.',
        'The sample library projects indexed four-window, 16-band acoustic fingerprints onto two principal components so similar spectra tend to appear near one another. Projection runs in a background worker; libraries over 3,000 samples show an even representative subset in the map while the full result list remains available. Older records without a fingerprint use duration and brightness.',
        'Sample similarity, exact and perceptual duplicate, velocity-layer, round-robin candidate, and portable metadata archive lookups run on background workers in the VST editor.',
        'The Plugin Scanner displays percentage progress, and VST3 discovery uses a cached timestamp index to avoid repeated linear searches while still rescanning changed modules.',
        'Sequencer Play follows host PPQ and BPM during DAW playback, schedules exact MIDI mappings in the audio callback, and reports hits that have no exact mapping. When the host is stopped, the plugin tempo preview remains available.',
        'Sequencer base-tempo edits, pattern bars, semantic hits, MIDI mapping and tempo/meter metadata are preserved in the host plugin state.',
        'Sample preview audio decodes in a cancellable background worker; newer preview requests supersede older ones. Pad and MIDI-map note labels use the Cubase octave convention (MIDI 36 = C1).',
        'Pad sample assignment and project-state restoration decode in cancellable background workers. Sample paths remain in the saved host state while restored audio is loading.',
        'Drum pads glow briefly when struck, using distinct colors for instrument groups. Each MIDI pad has independent gain, pan, tuning, Drive, three-band EQ, attack, decay, sustain, release and low-pass cutoff controls saved with the DAW project.',
        'Cubase can automate all 24 sound controls for each of the 16 pads; parameters are grouped by pad and saved with the project.',
        'Each pad can also be assigned to choke group Off or 1-16; triggering another pad in that group cuts its active voice, and the setting is automatable and saved with the project.',
        'The VST opens and closes Cubase automation gestures around UI edits, including rotary-control drags and sample-trim waveform drags.',
        'The Drum Machine waveform includes per-pad sample start and end handles. Drag either marker to trim playback, double-click to reset the full sample, and save the range with the DAW project.',
        'The Drum Machine Layers menu can preview, edit the velocity range of, or remove an individual sample layer. Ranges are validated against other layers on the pad and persist with the DAW project.',
        'MIDI export defaults to exact articulation mappings. The VST, desktop and mobile controls offer no fallback, default-articulation-only fallback, or any same-instrument articulation. Each substitution and omitted event is reported after export.',
        'MIDI import processes events across all tracks in musical time order, preserving overlapping same-pitch note durations split between tracks.',
        'MIDI imports also accept valid SMPTE absolute-time divisions at 24, 25, 29.97 drop-frame, and 30 fps, converting through tempo changes and exporting in PPQ format.',
        'Round-robin counters are independent per MIDI note and velocity group, so hits in another velocity range do not advance that group.',
        'The VST reports the duration of its longest loaded sample as the DAW tail length, including assigned samples and the active preview.',
        'The installed-library scan includes Native Instruments shared Public Documents content, Steinberg shared and per-user Roaming AppData content, Cubase 15 Media and VST Sound locations, and detected Spectrasonics locations.',
        'The manual-example classifier selects an acoustic fingerprint weight using category-stratified validation with query filenames hidden. It remains a local, low-confidence review aid; the current folder-labeled collections do not demonstrate reliable audio-only or cross-kit classification.',
        'Conservative audio-only review suggestions cover low-centroid kicks, noisy cymbals and hi-hats, high pitched bell-like percussion and a narrow low-mid snare range. The 121-file CC0 single-kit corpus scored 70.2% accuracy / 0.737 macro F1; a separate 52-file three-kit corpus scored 48.1% / 0.420. All such suggestions remain below the 0.75 acceptance threshold. These limited corpus results do not establish general classifier accuracy.',
        'Generic HiHat/HH filename cues are searchable as Hi-hat but no longer imply Closed; contradictory open/closed cues remain Unknown.',
        'Opt-in metadata folder sync is available from the standalone app and VST. It shares tags, manual categories, and MIDI maps only; it never copies samples, paths, or plug-in states. Merge is transactional, and cloud-provider upload completion still needs to be confirmed by the user.'
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
        'Linux x64 was built and tested in WSL Ubuntu 24.04. CTest loads the byte-identical release VST3 through JUCE, scans MIDI notes 0-127 and confirms audible note 36. Ardour 8.4 also recognizes the package; interactive Ardour session and physical Linux audio-device tests remain pending.',
        'The Linux VST3 bundles MagDrumScanner under Contents/Resources for isolated instrument discovery and note scans. The helper executable is included and dependency-checked; native DAW/plugin runtime validation remains pending.'
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
