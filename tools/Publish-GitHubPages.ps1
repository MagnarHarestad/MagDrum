[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$websiteRepository = 'MagnarHarestad/MagDrum-Website'
$latestRelease = & gh release list --repo 'MagnarHarestad/MagDrum' --limit 100 --json tagName,isPrerelease --jq 'map(select(.isPrerelease))[0].tagName'
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($latestRelease)) {
    throw 'Could not resolve the current MagDrum GitHub release.'
}

$siteRoot = Join-Path $env:TEMP ('magdrum-pages-' + [guid]::NewGuid().ToString('N'))
& gh repo clone $websiteRepository $siteRoot
if ($LASTEXITCODE -ne 0) { throw 'Could not clone the public MagDrum website repository.' }

$landingPage = [System.IO.File]::ReadAllText((Join-Path $repoRoot 'docs/index.html'))
$landingPage = $landingPage.Replace('@LATEST_RELEASE@', $latestRelease)
$landingPage = $landingPage.Replace(
    'https://github.com/MagnarHarestad/MagDrum/releases/latest',
    "https://github.com/MagnarHarestad/MagDrum/releases/tag/$latestRelease")
$landingPage = $landingPage.Replace(
    '<a href="https://github.com/MagnarHarestad/MagDrum">Source</a>',
    '<a href="https://github.com/MagnarHarestad/MagDrum/releases">Releases</a>')
$landingPage = $landingPage.Replace(
    '<a href="https://github.com/MagnarHarestad/MagDrum">Source on GitHub</a>',
    '<a href="https://github.com/MagnarHarestad/MagDrum/releases">MagDrum releases</a>')
$landingPage = $landingPage.Replace(
    '<a href="https://github.com/MagnarHarestad/MagDrum/blob/main/docs/installing.md">Installation guide</a> &nbsp;·&nbsp; <a href="https://github.com/MagnarHarestad/MagDrum/issues">Report a problem</a>',
    "<a href=`"https://github.com/MagnarHarestad/MagDrum/releases/tag/$latestRelease`">Download package and installation instructions</a>")
$utf8 = [System.Text.UTF8Encoding]::new($false)
[System.IO.File]::WriteAllText((Join-Path $siteRoot 'index.html'), $landingPage, $utf8)
Copy-Item -LiteralPath (Join-Path $repoRoot 'docs/style.css') -Destination (Join-Path $siteRoot 'style.css') -Force
[System.IO.File]::WriteAllText((Join-Path $siteRoot '.nojekyll'), '', $utf8)

$siteReadme = @"
# MagDrum Website

Public GitHub Pages site for MagDrum: https://magnarharestad.github.io/MagDrum-Website/

The application source repository is public: https://github.com/MagnarHarestad/MagDrum

This repository contains only the product landing page and stylesheet. The release script syncs these files from the application repository's `docs` directory after each release.
"@
[System.IO.File]::WriteAllText((Join-Path $siteRoot 'README.md'), $siteReadme, $utf8)

git -C $siteRoot add index.html style.css .nojekyll README.md
if ($LASTEXITCODE -ne 0) { throw 'Could not stage website files.' }
$pending = git -C $siteRoot status --porcelain
if ($LASTEXITCODE -ne 0) { throw 'Could not inspect website changes.' }
if (-not $pending) {
    Write-Host 'The public website is already current.'
    return
}
git -C $siteRoot -c user.name='Magnar Harestad' -c user.email='magnar.harestad@cgi.com' commit -m "Update MagDrum website for $latestRelease"
if ($LASTEXITCODE -ne 0) { throw 'Could not commit website files.' }
git -C $siteRoot push origin main
if ($LASTEXITCODE -ne 0) { throw 'Could not push the public website update.' }
Write-Host "Published MagDrum website for $($latestRelease): https://magnarharestad.github.io/MagDrum-Website/"






