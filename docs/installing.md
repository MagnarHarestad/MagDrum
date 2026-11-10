# Installing MagDrum test builds

Download the archive for your platform from the GitHub release. These early
builds are not code-signed, so the operating system may ask you to confirm that
you trust the application.

The latest published test build is [v0.1.1-test.72](https://github.com/MagnarHarestad/MagDrum/releases/tag/v0.1.1-test.72), with Windows x64 and ARM64 packages. The x64 package was built locally without GitHub Actions and both CTest suites passed. Windows ARM64 was cross-compiled; its PE architecture was checked, but ARM64 test executables could not run on this x64 host. Native ARM64 runtime testing remains outstanding. JUCE omitted the optional ARM64 `moduleinfo.json`; hosts can still query the VST3 module factory, though scanning may take longer. Separate Intel and Apple Silicon macOS archives and Linux x64 have not been built or published; see [local build requirements](local-builds.md).

The x64 archive SHA-256 is `7e5b0dd6b9642c191695793720179abf7091c1fa5cf067d39278abfba677b146`; the ARM64 archive SHA-256 is `dba39569d4e24c615091cb15946b975bb35d198c4097ac7468c3a846eeb011e6`. Both archives contain the standalone app, VST3 bundle and installer. The installer warns if a system-wide copy is present and explains how to replace it while keeping a backup.

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
