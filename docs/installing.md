# Installing MagDrum test builds

Download the archive for your platform from the GitHub release. These early
builds are not code-signed, so the operating system may ask you to confirm that
you trust the application.

The latest published archive is the [Windows x64 test build v0.1.1-test.67](https://github.com/MagnarHarestad/MagDrum/releases/tag/v0.1.1-test.67). It was built locally without GitHub Actions and passed both CTest suites, including SFZ filename-family classification checks, a headless end-to-end test of the built VST3 through the isolated note scanner, spectral-flatness fixtures, analyzed pitched-tom and four-pulse Clap WAV fixtures, transient-count persistence, and 250,000-row database pagination. The app and VST3 both report version 0.1.1. Windows ARM64 has been compiled and packaged locally but is not published because its VST3 bundle is missing JUCE-generated `moduleinfo.json`. Separate Intel and Apple Silicon macOS archives and Linux x64 have not been built or published; see [local build requirements](local-builds.md).

The v0.1.1-test.67 archive has SHA-256 `a1c542ba4aa8f95935539b775b3fe90845b1d66cf71cd225b54c4226d32be301`, matching the local `latest-publish/MagDrum-Windows-x64.zip`. It contains the standalone app, VST3 bundle and installer, and both CTest suites pass. The installer warns if a system-wide copy is present and explains how to replace it while keeping a backup. The previous v0.1.1-test.66 package had SHA-256 `66007d7d36f2a08fae0e80f6c69582d52a3547f4ceba60baee4fbc5a9fa3bde9`.

## Windows x64 and Cubase

Unzip `MagDrum-Windows-x64.zip`, close Cubase, then run `Install-MagDrum.ps1`
from PowerShell. It installs into your per-user VST3 folder without administrator
access. If a system-wide MagDrum VST3 is already installed, the installer warns
that Cubase may discover both copies. To update the system-wide copy, run
PowerShell as administrator and add `-AllUsers`; the installer asks before
replacing it and keeps a backup. Start Cubase 15 and rescan plugins in Studio >
VST Plug-in Manager. Add MagDrum as an instrument track. The archive also
contains the standalone sample library app.

## Windows ARM64

The local `latest-publish/MagDrum-Windows-ARM64.zip` contains ARM64 app and VST3
binaries, confirmed by their PE architecture. JUCE could not run its ARM64 VST3
manifest helper on this AMD64 build host, so the bundle has no `moduleinfo.json`
and the archive is not a release candidate. Use the published x64 package with
x64 Cubase; an ARM64 Cubase installation would require an ARM64 plugin package
with valid manifest metadata.

## macOS Intel and Apple Silicon

Unzip `MagDrum-macOS-Intel.zip` on Intel Macs or
`MagDrum-macOS-AppleSilicon.zip` on Apple Silicon. Move `MagDrum.app` to Applications. To install the
plugin formats, copy `MagDrum.vst3` to `~/Library/Audio/Plug-Ins/VST3` and
`MagDrum.component` to `~/Library/Audio/Plug-Ins/Components`, creating those
folders if needed. Restart the DAW and rescan its plugins. Separate native
architecture builds are configured; neither archive is published yet.

## Linux x64

Extract `MagDrum-Linux-x64.tar.gz`. Launch `linux-x64/MagDrum` for the
standalone app. Copy `linux-x64/MagDrum.vst3` to `~/.vst3` and rescan plugins
in your DAW. The host system must provide the GUI/audio libraries used by the
build.

These archives are test packages, not signed commercial installers. AUv3 and
iOS/iPadOS packages are not available yet.
