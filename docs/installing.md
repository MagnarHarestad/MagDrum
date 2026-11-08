# Installing MagDrum test builds

Download the archive for your platform from the GitHub release. These early
builds are not code-signed, so the operating system may ask you to confirm that
you trust the application.

The currently available archive is Windows x64. The macOS and Linux sections
describe the intended package layout; those archives are not published yet.

## Windows x64 and Cubase

Unzip `MagDrum-Windows-x64.zip`, close Cubase, then run `Install-MagDrum.ps1`
from PowerShell. Approve the administrator prompt so it can copy
`MagDrum.vst3` into the system VST3 folder. Start Cubase 15 and rescan plugins
in Studio > VST Plug-in Manager. Add MagDrum as an instrument track. The
archive also contains the standalone sample library app.

## macOS

Unzip `MagDrum-macOS.zip`. Move `MagDrum.app` to Applications. To install the
plugin formats, copy `MagDrum.vst3` to `~/Library/Audio/Plug-Ins/VST3` and
`MagDrum.component` to `~/Library/Audio/Plug-Ins/Components`, creating those
folders if needed. Restart the DAW and rescan its plugins. The package is built
on the current GitHub macOS runner; Intel compatibility has not been verified.

## Linux x64

Extract `MagDrum-Linux-x64.tar.gz`. Launch `linux-x64/MagDrum` for the
standalone app. Copy `linux-x64/MagDrum.vst3` to `~/.vst3` and rescan plugins
in your DAW. The host system must provide the GUI/audio libraries used by the
build.

These archives are test packages, not signed commercial installers. AUv3 and
iOS/iPadOS packages are not available yet.
