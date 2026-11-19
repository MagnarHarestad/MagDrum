# MagDrum

MagDrum is a drum sample library, VST3 drum machine and semantic MIDI workspace. The active product target is Windows x64 in Cubase Pro 15. Linux and macOS work is paused until explicitly requested.

## Current Windows build

The latest Windows x64 test release is [v0.1.2-test.272](https://github.com/MagnarHarestad/MagDrum/releases/tag/v0.1.2-test.272), VST3 version 0.1.2.272. The Plugin Scanner now searches standard or selected folders for VST3 presets in a cancellable background task; choose a result in the VST menu and import it as a kit. Release CTest passed 3 applicable tests (the Debug-only assertion test was skipped); localization (629 labels), package checks, Steinberg VST3 validation (47/47), exact-package state restoration, MIDI 0-127 rendering and 12 offscreen UI renders passed. Release is published; .271 remains installed per user pending the broader Windows feature pass. Cubase and third-party instrument behavior remain unverified. ZIP SHA-256: `8ed45cf9ba503c02385c35c4fcc8cfbe8dab845da0e8e321c4746f8e2cef6dba`; module SHA-256: `8068C02E2045883FA796F6B0FADE8BA64146AF4CCC6BB8D8D5C3E55AF528CE75`.

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
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\Test-InstalledWindowsVST3.ps1 -ExpectedVersion 0.1.2.232
```

See [`docs/vst-parity.md`](docs/vst-parity.md#windows-development-and-installation-loop) for the full workflow. Do not use `Build-Unix.sh` while Linux and macOS remain paused.

## Website and license

The public [MagDrum website](https://magnarharestad.github.io/MagDrum-Website/) is published from the [MagDrum-Website repository](https://github.com/MagnarHarestad/MagDrum-Website).

MagDrum's original source code is licensed under [GNU AGPLv3](LICENSE). JUCE is used under its AGPLv3 option; third-party components retain their own terms. See [`docs/licensing.md`](docs/licensing.md) and [`THIRD-PARTY-NOTICES.md`](THIRD-PARTY-NOTICES.md).

No sample libraries or audio content are included.
