# Installing MagDrum test builds

Download the archive for your platform from the GitHub release. These early
builds are not code-signed, so the operating system may ask you to confirm that
you trust the application.

The latest packages are in [v0.1.1-test.151](https://github.com/MagnarHarestad/MagDrum/releases/tag/v0.1.1-test.151): Windows x64, Windows ARM64, and Linux x64. The Windows VST library scan includes Cubase 15 Media folders. The VST includes the eight-language picker, per-pad gain/pan/tune controls, a bounded UI-to-audio trigger queue for pad and sequencer playback, and a sequencer grid that follows supported MIDI time-signature changes. Sequencer playback follows the DAW's PPQ and tempo when its transport is running, and uses the plugin preview tempo when stopped. MIDI export is exact-mapping-only by default; enable articulation fallback when desired, and review the per-event fallback/omission report. The sample browser uses a persistent SQLite FTS5 trigram index for substring search across file names, paths, tags, sources, plugins and kits; migration tests verify existing library rows are rebuilt, and a 250,000-row fixture confirms the virtual index query path. The sample-sort selector and library scan summary use ASCII punctuation to avoid code-page glyph corruption; language translations retain their native characters. Preset, MIDI, profile and metadata file chooser titles use the selected language. Velocity-layer and round-robin review dialogs, candidate reports, confirmation controls and results are also translated. English is the first-run default; a selected language is saved and shared by the VST3 and standalone app. The VST sample picker loads WAV, AIFF and FLAC files directly, including when the sample index is unavailable. Windows x64 and Linux Release CTest suites pass (2/2 each). Windows ARM64 was cross-compiled from x64, but it has not been run on native ARM64 Windows; JUCE omits the optional ARM64 `moduleinfo.json`, so hosts may take longer to scan it. Linux CTest loads the release module and verifies note capture; Ardour 8.4 recognizes it, while interactive Linux DAW/audio testing and macOS packages remain pending; see [local build requirements](local-builds.md).

The latest package archives contain the standalone app, VST3 bundle, installer, MagDrum's AGPLv3 `LICENSE`, and JUCE and bundled-component license notices, including FLAC, Ogg Vorbis/libvorbis, JPEG, PNG, zlib, HarfBuzz and SheenBidi. The installer keeps previous plug-in bundles under `%LOCALAPPDATA%\MagDrum\VST3-backups` (per-user) or `%ProgramData%\MagDrum\VST3-backups` (all-users), outside Cubase's VST3 scan folders. After installing, use **Rescan All** in Cubase's VST Plug-in Manager so it drops cached entries for older copies. Check the release page for the matching package checksums.

## Windows x64 and Cubase

For a step-by-step Cubase Pro 15 play, state-save and velocity-layer checklist,
see [Verify MagDrum in Cubase Pro 15](cubase-testing.md).

Unzip `MagDrum-Windows-x64.zip`, close Cubase completely, then run `Install-MagDrum.ps1`. The installer checks for running Cubase processes (including hidden processes with no window) and refuses to replace the plug-in until they exit.
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

Extract `MagDrum-Linux-x64.tar.gz`. Launch `MagDrum` for the standalone app, or
run `./Install-MagDrum-Linux.sh` to install/update it and the VST3 to your user
folders. Previous versions are moved outside the VST3 scan path. The Linux VST3 bundle includes the MagDrum scanner helper for isolated instrument discovery and note scans; runtime with native Linux instruments and DAWs still needs validation. The host
system must provide the GUI/audio libraries used by the build.

These archives are test packages, not signed commercial installers. AUv3 and
iOS/iPadOS packages are not available yet.
