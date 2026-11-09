# Installing MagDrum test builds

Download the archive for your platform from the GitHub release. These early
builds are not code-signed, so the operating system may ask you to confirm that
you trust the application.

The latest published test build is [v0.1.1-test.68](https://github.com/MagnarHarestad/MagDrum/releases/tag/v0.1.1-test.68), with Windows x64 and ARM64 packages. The x64 package was built locally without GitHub Actions and passed both CTest suites, including a headless end-to-end test of the built VST3 through the isolated note scanner. Windows ARM64 was cross-compiled and its binary architecture checked, but it has not been runtime-tested on ARM64 Windows. JUCE omitted the optional `moduleinfo.json`; hosts can still query the VST3 module factory, though scanning may take longer. Separate Intel and Apple Silicon macOS archives and Linux x64 have not been built or published; see [local build requirements](local-builds.md).

The v0.1.1-test.68 x64 archive has SHA-256 `a1c542ba4aa8f95935539b775b3fe90845b1d66cf71cd225b54c4226d32be301`, matching the local package. The ARM64 archive SHA-256 is `49caafb4e6dfb158102328e0b131da25dc696dd1cde645c83de1dd429ee47de6`. Both archives contain the standalone app and VST3 bundle; x64 also includes the installer. The x64 package passed both CTest suites. The installer warns if a system-wide copy is present and explains how to replace it while keeping a backup.

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

The published `MagDrum-Windows-ARM64.zip` contains ARM64 app and VST3 binaries,
confirmed by their PE architecture. They have not been runtime-tested on ARM64
Windows. JUCE could not run its ARM64 VST3 manifest helper on this AMD64 build
host, so the bundle has no `moduleinfo.json`. Steinberg marks that metadata file
optional; a host can query the module factory instead, which may take longer.
For Cubase Pro 15 on Windows x64, use the Windows x64 package above.

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
