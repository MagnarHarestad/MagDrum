# Installing MagDrum test builds

Download the archive for your platform from the GitHub release. These early
builds are not code-signed, so the operating system may ask you to confirm that
you trust the application.

The latest Windows x64 package is [v0.1.2-test.199](https://github.com/MagnarHarestad/MagDrum/releases/tag/v0.1.2-test.199), with VST3 product version 0.1.2.199. Samples assigned to the same velocity range now play in round-robin order, and the variant group persists in Cubase state. Installed-library scanning reports Steinberg `.vstsound` and Kontakt `.ncw` content that cannot be decoded directly and points to Plugin Scanner for instrument audio capture. MIDI export offers no fallback, default-articulation-only fallback, or any mapped articulation from the same instrument; exact mappings remain the default and substitutions are reported. MIDI import processes events across all tracks in musical time order to preserve overlapping note durations. Cubase displays the named default program, and a host-side program rename is stored in plug-in state. The sample map labels brighter sounds at the top and darker sounds at the bottom, and hover shows filename, category, duration and brightness. Contextual tooltips are available throughout the VST. Sequencer tempo, pattern length, semantic hits, MIDI mapping, tempo/meter metadata and pending sample paths persist in Cubase plug-in state. Sample preview, pad assignment and project-state restoration decode audio in cancellable background workers. The sample-comparison window now previews either waveform through the instrument output for A/B listening, with Stop controls in both views. If the editor is open during state restoration, its waveform and velocity-layer summary refresh when loading completes. Drum-pad pitch labels use the Cubase octave convention. Installed-library scanning includes Steinberg content under the current user's Roaming AppData folder, in addition to shared ProgramData and detected Cubase paths. Similar-sample, exact/perceptual duplicate, velocity-layer, round-robin and portable metadata archive operations run in background workers so large indexes do not block the VST editor. Windows x64 Release CTest passes 2/2, including MIDI playback and audio-thread allocation checks. The package bundles the Microsoft-signed Visual C++ runtime required by the build; the installer updates it only if it is missing or older. Windows ARM64 and Linux x64 packages remain in [v0.1.1-test.156](https://github.com/MagnarHarestad/MagDrum/releases/tag/v0.1.1-test.156); ARM64 lacks native runtime testing, and Linux requires DAW/audio review. The Windows ZIP includes `Install-MagDrum-User.cmd` for a per-user install under `%LOCALAPPDATA%`, which needs no UAC after one-time migration of older system copies. That migration can request one UAC approval to move MagDrum bundles out of VST3 scan paths. Use `Install-MagDrum.cmd` for a system-wide install. Linux and macOS builds are paused until requested.

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
