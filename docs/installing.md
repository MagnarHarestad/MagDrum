# Installing MagDrum test builds

Download the archive for your platform from the GitHub release. These early
builds are not code-signed, so the operating system may ask you to confirm that
you trust the application.

The latest Windows x64 package is [v0.1.2-test.267](https://github.com/MagnarHarestad/MagDrum/releases/tag/v0.1.2-test.267), with VST3 product version 0.1.2.267. Release CTest passed 4/4; localization passed for 622 labels; package safeguards passed. The exact packaged module passed isolated project-state restore and MIDI 0–127 rendering including C1. Steinberg VST 3.8.1 validation passed 47/47 standard and 537/537 extended tests. Eleven Release offscreen images cover all five views, both Sample Library filter states, the in-app MIDI editor and picker, and both compact effect-panel sizes. The .267 note scanner handles multi-output instruments with up to 64 channels and captures the strongest stereo pair. ZIP SHA-256: `3aac1de574973458d0ff4ec5180193275d3ac07559f8eb7ac217e55c203c197d`; module SHA-256: `686E112BBAF0113008D0DBAECE13752A0CB3A7AF4B45760BE3F2CE7E0E226141`. The .267 prerelease is published but not installed; .252 remains installed per-user. Battery 4's saved default kit was empty and rendered no audible captures; populated-kit behavior, Retrologue, Groove Agent SE and Cubase Pro 15 host interaction remain unverified.

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
