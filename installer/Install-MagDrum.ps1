$ErrorActionPreference = 'Stop'
$source = Join-Path $PSScriptRoot 'MagDrum.vst3'
if (-not (Test-Path $source)) { throw 'MagDrum.vst3 was not found beside this installer.' }
$destination = Join-Path $env:CommonProgramFiles 'VST3\MagDrum.vst3'
$parent = Split-Path $destination
New-Item -ItemType Directory -Force $parent | Out-Null
if (Test-Path $destination) { Remove-Item -LiteralPath $destination -Recurse -Force }
Copy-Item -LiteralPath $source -Destination $destination -Recurse
Write-Host "Installed MagDrum VST3 to $destination"
Write-Host 'Restart Cubase and run Studio > VST Plug-in Manager > Rescan All.'
