# Installing MagDrum test builds

Download the archive for your platform from the GitHub release. These early
builds are not code-signed, so the operating system may ask you to confirm that
you trust the application.

The currently available archive is the [Windows x64 test build v0.1.1-test.26](https://github.com/MagnarHarestad/MagDrum/releases/tag/v0.1.1-test.26). It was built, tested and uploaded locally without GitHub Actions. Windows ARM64, separate Intel and Apple Silicon macOS archives, and Linux x64 have not been built or published yet; see [local build requirements](local-builds.md).

## Windows x64 and Cubase

Unzip `MagDrum-Windows-x64.zip`, close Cubase, then run `Install-MagDrum.ps1`
from PowerShell. It installs into your per-user VST3 folder without administrator
access. For an all-users install, run PowerShell as administrator and add
`-AllUsers`. Start Cubase 15 and rescan plugins
in Studio > VST Plug-in Manager. Add MagDrum as an instrument track. The
archive also contains the standalone sample library app.

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
