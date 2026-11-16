# Installing MagDrum test builds

Download the archive for your platform from the GitHub release. These early
builds are not code-signed, so the operating system may ask you to confirm that
you trust the application.

The latest Windows x64 package is [v0.1.2-test.173](https://github.com/MagnarHarestad/MagDrum/releases/tag/v0.1.2-test.173), with VST3 product version 0.1.2.173 and selectable 1/8, 1/16 and 1/32 sequencer grids. Similar-sample, exact/perceptual duplicate, velocity-layer, round-robin and portable metadata archive operations run in background workers so large indexes do not block the VST editor. Windows x64 Release CTest passes 2/2, including MIDI playback and audio-thread allocation checks. The package bundles the Microsoft-signed Visual C++ runtime required by the build; the installer updates it only if it is missing or older. Windows ARM64 and Linux x64 packages remain in [v0.1.1-test.156](https://github.com/MagnarHarestad/MagDrum/releases/tag/v0.1.1-test.156); ARM64 lacks native runtime testing, and Linux requires DAW/audio review. Linux and macOS builds are paused until requested.

The latest package archives contain the standalone app, VST3 bundle, installer, MagDrum's AGPLv3 `LICENSE`, and JUCE and bundled-component license notices, including FLAC, Ogg Vorbis/libvorbis, JPEG, PNG, zlib, HarfBuzz and SheenBidi. The installer keeps previous plug-in bundles under `%LOCALAPPDATA%\MagDrum\VST3-backups` (per-user) or `%ProgramData%\MagDrum\VST3-backups` (all-users), outside Cubase's VST3 scan folders. After installing, use **Rescan All** in Cubase's VST Plug-in Manager so it drops cached entries for older copies. Check the release page for the matching package checksums.

## Windows x64 and Cubase

For a step-by-step Cubase Pro 15 play, state-save and velocity-layer checklist,
see [Verify MagDrum in Cubase Pro 15](cubase-testing.md).

Unzip `MagDrum-Windows-x64.zip`, close Cubase, and run `Install-MagDrum.ps1` from PowerShell. The installer refreshes process records and blocks only while Cubase is still running; an exited process record is ignored. By default it installs to the per-user VST3 folder without administrator access. To replace an existing system-wide copy, run PowerShell as administrator with `-AllUsers -Force`; the old bundle is moved to `%ProgramData%\MagDrum\VST3-backups`, outside the VST3 scan folder. Use `-Force` only for a replacement you have authorized. Start Cubase 15 and rescan plugins in Studio > VST Plug-in Manager. Add MagDrum as an instrument track. The archive also contains the standalone sample library app.

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
