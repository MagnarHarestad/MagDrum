$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$sourceKeys = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::Ordinal)
$sourceFiles = Get-ChildItem -LiteralPath (Join-Path $repoRoot 'src') -Recurse -File |
    Where-Object { $_.Extension -in @('.cpp', '.h') }

foreach ($file in $sourceFiles) {
    $source = [System.IO.File]::ReadAllText($file.FullName)
    foreach ($match in [regex]::Matches($source, 'localisedText\s*\(\s*"((?:[^"\\]|\\.)*)"')) {
        $key = $match.Groups[1].Value
        if ($key -match '\p{L}' -and $key -notin @('MagDrum', 'General MIDI')) {
            [void] $sourceKeys.Add($key)
        }
    }
}

$catalogSource = [System.IO.File]::ReadAllText((Join-Path $repoRoot 'src/ui/MagDrumLocalization.cpp'))
$catalogBlocks = [regex]::Matches(
    $catalogSource,
    'constexpr auto (german|french|spanish|swedish|norwegian|danish|finnish)\s*=\s*R"translations\((.*?)\)translations"',
    [System.Text.RegularExpressions.RegexOptions]::Singleline)
$expectedLanguages = @('german', 'french', 'spanish', 'swedish', 'norwegian', 'danish', 'finnish')
if ($catalogBlocks.Count -ne $expectedLanguages.Count) {
    throw "Expected $($expectedLanguages.Count) non-English localization catalogs, found $($catalogBlocks.Count)."
}

$failureCount = 0
foreach ($block in $catalogBlocks) {
    $language = $block.Groups[1].Value
    $catalogKeys = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::Ordinal)
    foreach ($match in [regex]::Matches($block.Groups[2].Value, '(?m)^"((?:[^"\\]|\\.)*)"\s*=')) {
        [void] $catalogKeys.Add($match.Groups[1].Value)
    }

    $missing = @($sourceKeys | Where-Object { -not $catalogKeys.Contains($_) } | Sort-Object)
    if ($missing.Count -gt 0) {
        $failureCount += $missing.Count
        Write-Error "$language is missing $($missing.Count) translated source label(s):`n  $($missing -join "`n  ")" -ErrorAction Continue
    }
}

if ($failureCount -gt 0) {
    throw "Localization validation failed: $failureCount missing translation(s)."
}

Write-Host "Localization validation passed: $($sourceKeys.Count) translatable source labels are present in all $($catalogBlocks.Count) non-English catalogs."
