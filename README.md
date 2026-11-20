# MagDrum

MagDrum is a drum sample library, VST3 drum machine and semantic MIDI workspace. The active product target is Windows x64 in Cubase Pro 15. Linux and macOS work is paused until explicitly requested.

## Current Windows build

The latest Windows x64 build is [v0.1.2-test.281](https://github.com/MagnarHarestad/MagDrum/releases/tag/v0.1.2-test.281), VST3 version 0.1.2.281. Battery's preset-search empty states now show its supported `.nbkt` workflow, and its in-app picker can select native kits without treating them as VST3 presets. Battery sample classification recognizes `ClosedHH`, `Perc`, Bell and cymbal naming, while explicit instrument names take priority over broad folder defaults. Windows Release CTest passed five applicable tests (the Debug-only assertion worker was skipped); localization and package checks passed. The exact package restored state and rendered MIDI 0–127 including C1. .281 is built and awaiting publication/installation. Direct Battery `.nbkt` import, populated Battery kit capture and Cubase behavior remain unverified. ZIP SHA-256: `632d781015e4a927116999d569d9b68bbd08c9210155c3fc2ce1dca2a4e0fcfa`; module SHA-256: `09B4E6807C61D077086031A604DF7E83847BEBDDD9E39BE352E0531045EB9A48`.

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
