# MagDrum

MagDrum is a drum sample library, VST3 drum machine and semantic MIDI workspace. The active product target is Windows x64 in Cubase Pro 15. Linux and macOS work is paused until explicitly requested.

## Current Windows build

The latest Windows x64 build is [v0.1.2-test.278](https://github.com/MagnarHarestad/MagDrum/releases/tag/v0.1.2-test.278), VST3 version 0.1.2.278. Instrument program lists are discovered in an isolated worker with progress and a 90-second timeout. Filename classification now recognizes Battery `ClosedHH` names and distinguishes China Gong from China cymbal samples. Windows x64 Release CTest passed 5 applicable tests (the Debug-only assertion worker was skipped); localization and package checks passed. The exact built package restored state and rendered MIDI 0–127 including C1. A read-only audit analyzed 100 Battery samples in each Kick, Snare, HiHat and Cymbal folder with zero read errors; all 100 HiHat samples were classified as Closed hi-hat. .278 is built and awaiting publication/installation. Cubase behavior and populated Battery kit capture remain unverified. ZIP SHA-256: `e19ded3557d679eb0d51434810f7423855bc4b730a86337c99b10ddd5d3497bc`; module SHA-256: `AFA6DEE822765BA920BDF89B71B8059CA9318342E69458D2575E6E3EB00C2EF4`.

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
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\Test-InstalledWindowsVST3.ps1 -ExpectedVersion 0.1.2.274
```

See [`docs/vst-parity.md`](docs/vst-parity.md#windows-development-and-installation-loop) for the full workflow. Do not use `Build-Unix.sh` while Linux and macOS remain paused.

## Website and license

The public [MagDrum website](https://magnarharestad.github.io/MagDrum-Website/) is published from the [MagDrum-Website repository](https://github.com/MagnarHarestad/MagDrum-Website).

MagDrum's original source code is licensed under [GNU AGPLv3](LICENSE). JUCE is used under its AGPLv3 option; third-party components retain their own terms. See [`docs/licensing.md`](docs/licensing.md) and [`THIRD-PARTY-NOTICES.md`](THIRD-PARTY-NOTICES.md).

No sample libraries or audio content are included.
