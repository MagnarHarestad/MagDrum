# Installing MagDrum test builds

Download the archive for your platform from the GitHub release. These early
builds are not code-signed, so the operating system may ask you to confirm that
you trust the application.

The latest published archive is the [Windows x64 test build v0.1.1-test.59](https://github.com/MagnarHarestad/MagDrum/releases/tag/v0.1.1-test.59). It was built locally without GitHub Actions and passed both CTest suites, including a headless end-to-end test of the built VST3 through the isolated note scanner. The app and VST3 both report version 0.1.1. Windows ARM64, separate Intel and Apple Silicon macOS archives, and Linux x64 have not been built or published yet; see [local build requirements](local-builds.md).

The current local `latest-publish/MagDrum-Windows-x64.zip` has SHA-256 `78f20cb8844304e41e772d7813adad4163e8a60e32d55ebb98d7f8276c0c6416`, matching the v0.1.1-test.59 release asset. It contains the standalone app, VST3 bundle and installer. The installer warns if a system-wide copy is present and explains how to replace it while keeping a backup. The previous v0.1.1-test.56 package had SHA-256 `f6794b7960c59d70c5b43ccd1a5703def7c2e362be7543172c9a20aee9d0505f`.

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

Windows ARM64 can be built locally from Windows x64 with Visual Studio's ARM64
C++ tools. The current Visual Studio installation does not include the required
ARM64 C++ component, so the local ARM64 build stopped before compilation and no
ARM64 package is available yet. Use the x64 package only on systems
that support x64 applications until an ARM64 release is available.

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
