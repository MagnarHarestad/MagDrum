# Installing MagDrum test builds

Download the archive for your platform from the GitHub release. These early
builds are not code-signed, so the operating system may ask you to confirm that
you trust the application.

The latest Windows x64 VST3 package is [v0.1.1-test.109](https://github.com/MagnarHarestad/MagDrum/releases/tag/v0.1.1-test.109); it includes the previous in-VST instrument discovery and preset/program import features, and adds direct language selection from the burger menu with immediate translated refresh across the VST3 and standalone views. Plugin-specific editor interaction still uses the instrument manager. The Windows ARM64 package remains available from [v0.1.1-test.103](https://github.com/MagnarHarestad/MagDrum/releases/tag/v0.1.1-test.103). The VST3 and standalone app offer English, German, French, Spanish, Swedish, Norwegian, Danish and Finnish from the burger menu; English is the first-run default. Language choice is shared by both apps, saved for future launches, and refreshes the library filters, category editor, drum pads, sequencer, MIDI mapping and plugin-scan controls. The ARM64 app, VST3 and test executables were cross-compiled on x64; the ARM64 tests and app have not been run on native ARM64 Windows. JUCE omitted the optional ARM64 `moduleinfo.json`; hosts can query the VST3 module factory, though scanning may take longer. Separate Intel and Apple Silicon macOS archives and Linux x64 have not been built or published; see [local build requirements](local-builds.md).

The latest package archives contain the standalone app, VST3 bundle, installer, MagDrum's AGPLv3 `LICENSE`, and JUCE/VST3 license notices. The installer keeps previous plug-in bundles under `%LOCALAPPDATA%\MagDrum\VST3-backups` (per-user) or `%ProgramData%\MagDrum\VST3-backups` (all-users), outside Cubase's VST3 scan folders. After installing, use **Rescan All** in Cubase's VST Plug-in Manager so it drops cached entries for older copies. Check the release page for the matching package checksums.

## Windows x64 and Cubase

For a step-by-step Cubase Pro 15 play, state-save and velocity-layer checklist,
see [Verify MagDrum in Cubase Pro 15](cubase-testing.md).

Unzip `MagDrum-Windows-x64.zip`, close Cubase, then run `Install-MagDrum.ps1`
from PowerShell. It installs into your per-user VST3 folder without administrator
access. If a system-wide MagDrum VST3 is already installed, the installer warns
that Cubase may discover both copies. To update the system-wide copy, run
PowerShell as administrator and add `-AllUsers`; the installer asks before
replacing it and moves older copies outside the VST3 scan folder. Start Cubase 15 and rescan plugins in Studio >
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
