# Installing MagDrum test builds

Download the archive for your platform from the GitHub release. These early
builds are not code-signed, so the operating system may ask you to confirm that
you trust the application.

The latest Windows x64 package is [v0.1.2-test.288](https://github.com/MagnarHarestad/MagDrum/releases/tag/v0.1.2-test.288), VST3 version 0.1.2.288. Release CTest passed 9/9 applicable tests (the Debug-only assertion worker was skipped); package checks passed. The exact ZIP passed state restore, MIDI 0–127 rendering, 12 offscreen UI captures and Steinberg VST 3.8.1 validation (47 standard and 537 extended checks). The package is public but was not installed; Cubase-host behavior remains unverified. ZIP SHA-256: `70d263fb32922530ddbb5caf65194527c6d4f5c722e5e7c7f084586a5d45f2ad`; module SHA-256: `C149EE32D8C60E264740FB6912AE817291FFF2BF08C7F08B7CBEEB50494E3E50`.

The latest package archives contain the standalone app, VST3 bundle, installer, MagDrum's AGPLv3 `LICENSE`, and JUCE and bundled-component license notices, including FLAC, Ogg Vorbis/libvorbis, JPEG, PNG, zlib, HarfBuzz and SheenBidi. The installer keeps previous plug-in bundles under `%LOCALAPPDATA%\MagDrum\VST3-backups` (per-user) or `%ProgramData%\MagDrum\VST3-backups` (all-users), outside Cubase's VST3 scan folders. After installing, use **Rescan All** in Cubase's VST Plug-in Manager so it drops cached entries for older copies. Check the release page for the matching package checksums.

## Windows x64 and Cubase

For a step-by-step Cubase Pro 15 play, state-save and velocity-layer checklist,
see [Verify MagDrum in Cubase Pro 15](cubase-testing.md).

Unzip `MagDrum-Windows-x64.zip`, close Cubase, and double-click `Install-MagDrum-User.cmd`. It installs to your per-user VST3 folder without UAC and moves any existing system-wide MagDrum copies out of VST3 scan folders through a one-time UAC approval. Future per-user updates need no UAC. For a system-wide installation, use `Install-MagDrum.cmd`. Start Cubase 15 and rescan plugins in Studio > VST Plug-in Manager. Add MagDrum as an instrument track. The archive also contains the standalone sample library app.

## Windows ARM64

The published `MagDrum-Windows-ARM64.zip` contains ARM64 app and VST3 binaries,
confirmed by their PE architecture. They have not been runtime-tested on ARM64
Windows. JUCE could not run its ARM64 VST3 manifest helper on this AMD64 build
host, so the bundle has no `moduleinfo.json`. Steinberg marks that metadata file
optional; a host can query the module factory instead, which may take longer.
For Cubase Pro 15 on Windows x64, use the Windows x64 package above.

## macOS Intel and Apple Silicon — paused

No macOS package is currently published, and macOS work is paused until requested. For future reference, if a package is supplied, unzip `MagDrum-macOS-Intel.zip` on Intel Macs or
`MagDrum-macOS-AppleSilicon.zip` on Apple Silicon. Move `MagDrum.app` to Applications. To install the
plugin formats, copy `MagDrum.vst3` to `~/Library/Audio/Plug-Ins/VST3` and
`MagDrum.component` to `~/Library/Audio/Plug-Ins/Components`, creating those
folders if needed. Restart the DAW and rescan its plugins. Separate native
architecture builds are configured; neither archive is published yet.

## Linux x64 — paused

Only the historical v0.1.1-test.156 Linux package exists; new Linux work is paused until requested. If using that existing archive, extract `MagDrum-Linux-x64.tar.gz`, launch `MagDrum` for the standalone app, or
run `./Install-MagDrum-Linux.sh` to install/update it and the VST3 to your user
folders. Previous versions are moved outside the VST3 scan path. The Linux VST3 bundle includes the MagDrum scanner helper for isolated instrument discovery and note scans; runtime with native Linux instruments and DAWs still needs validation. The host
system must provide the GUI/audio libraries used by the build.

These archives are test packages, not signed commercial installers. AUv3 and
iOS/iPadOS packages are not available yet.
