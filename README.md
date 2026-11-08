# MagDrum

MagDrum is a cross-platform drum sound library and MIDI mapping workspace. Browse WAV sample libraries alongside sounds from drum instruments, audition them, and transfer drum patterns through semantic MIDI mappings.

**MagDrum** is the product name. Formats are platform targets: VST3 on Windows, macOS and Linux; AU on macOS; AUv3 on iOS/iPadOS where supported. Phase 1 is a standalone app; it does not require a DAW or commercial instruments.

**Website:** [magnarharestad.github.io/MagDrum](https://magnarharestad.github.io/MagDrum/)

## Phase 1 foundation

This repository contains a JUCE/CMake desktop app, background WAV/AIFF/FLAC scanning (subject to JUCE decoder support) with cancellation, a version-upgraded SQLite sample index, audio measurements, basic heuristic classification, category correction, user tags, similarity lookup, semantic drum-event and GM MIDI mapping, custom profile persistence/editing, and PPQ MIDI import/export. The desktop app can discover installed audio plugins with JUCE's plugin list and persist the inventory. The MagDrum VST3 loads user-selected audio samples from file or the shared SQLite index onto MIDI notes and saves sample paths with plugin state.

The current classifier is an early heuristic and the initial spectral measurement uses one analysis window. It is not a validated tagging model or perceptual fingerprint. Desktop plugin discovery is implemented, while selecting and hosting scanned instruments, kit/note rendering and indexing captured plugin sounds remain in progress. AU/AUv3 and iOS remain roadmap work.

## Build

Requires CMake 3.22+, C++20 and Git. JUCE and SQLite are fetched at pinned versions by CMake.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build --output-on-failure
```

See [roadmap](docs/roadmap.md) and [architecture](docs/architecture.md). The project license is undecided; keep this repository private until licensing is settled. No proprietary SDKs or copyrighted sample libraries are included.

See the current [semantic MIDI model](docs/midi-mapping.md) for its behavior and known mapping limitations.
