# MagDrum

MagDrum is a drum sample library, VST3 drum machine and semantic MIDI workspace. The active product target is Windows x64 in Cubase Pro 15. Linux and macOS work is paused until explicitly requested.

## Current Windows build

The latest test release is [v0.1.2-test.223](https://github.com/MagnarHarestad/MagDrum/releases/tag/v0.1.2-test.223), VST3 version 0.1.2.223. Release CTest passed 2/2, package verification passed, and the installed plugin passed isolated scanning and rendering for all 128 MIDI notes, including C1. Sample scans display percent, completed file count and current filename. Sequencer smoke coverage checks all 16 lane labels and General MIDI notes. VST text fields are vertically centered, including the library search and plugin-scan controls. Pads glow for mouse, sequencer and incoming host MIDI triggers. The installed per-user plugin is at `%LOCALAPPDATA%\Programs\Common\VST3\MagDrum.vst3`; the previous .222 bundle is backed up outside the VST3 scan folders. No system-wide copy is installed. Cubase Pro 15 rescan and hands-on acceptance remain pending.

The VST has five views: Sample Library, Drum Machine, Sequencer, MIDI Maps and Plugin Scanner. The drum machine follows a DR-660-inspired 4×4 order and provides velocity layers, round-robin playback, per-pad gain, pan, tune, Drive, three-band EQ, ADSR, low-pass and editable sample-trim controls. These settings are saved with the Cubase project. The VST scans local sample libraries, previews and assigns sounds, edits semantic drum patterns and mappings, and can discover VST3 instruments and capture their MIDI notes in an isolated helper. Third-party plugin behavior still needs real host review; opening a vendor instrument's own editor uses a separate MagDrum window.

## Product notes

- The shared browser indexes WAV, AIFF and FLAC samples, with search, category/source/kit filters, audition, waveform comparison, duplicates and acoustic-similarity views. It reports unsupported `.vstsound` and Kontakt `.ncw` files and offers plugin capture as an alternative.
- MIDI patterns use semantic drum events and configurable General MIDI or custom maps. Import/export preserves supported timing and reports ambiguous or unmapped notes. The sequencer follows Cubase transport when the host is playing.
- English, German, French, Spanish, Swedish, Norwegian, Danish and Finnish are available from the menu; English is the default.
- Classification remains a heuristic. Confidence is not calibrated, and the audio-only classifier needs broader human-reviewed evaluation.
- The Windows x64 build and Cubase test guide are maintained in [`docs/vst-parity.md`](docs/vst-parity.md) and [`docs/cubase-testing.md`](docs/cubase-testing.md).

## Build on Windows

Requires CMake 3.22+, C++20, Git and a supported Visual Studio C++ toolchain. JUCE and SQLite are fetched at pinned versions by CMake.

Use `Build-Windows-Plugin.ps1` for fast cached Debug iteration. Build and run the Windows Release checks once for a Cubase-ready milestone with `Build-Windows.ps1 -BuildRevision <test-number>`. The latter creates the package under `latest-publish`. For installed-package verification, run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\Test-InstalledWindowsVST3.ps1 -ExpectedVersion 0.1.2.223
```

See [`docs/vst-parity.md`](docs/vst-parity.md#windows-development-and-installation-loop) for the full workflow. Do not use `Build-Unix.sh` while Linux and macOS remain paused.

## Website and license

The public [MagDrum website](https://magnarharestad.github.io/MagDrum-Website/) is published from the [MagDrum-Website repository](https://github.com/MagnarHarestad/MagDrum-Website).

MagDrum's original source code is licensed under [GNU AGPLv3](LICENSE). JUCE is used under its AGPLv3 option; third-party components retain their own terms. See [`docs/licensing.md`](docs/licensing.md) and [`THIRD-PARTY-NOTICES.md`](THIRD-PARTY-NOTICES.md).

No sample libraries or audio content are included.
