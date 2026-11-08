# MagDrum

MagDrum is a cross-platform drum sound library and MIDI mapping workspace. Browse WAV sample libraries alongside sounds from drum instruments, audition them, and transfer drum patterns through semantic MIDI mappings.

**MagDrum** is the product name. Formats are platform targets: VST3 on Windows, macOS and Linux; AU on macOS; AUv3 on iOS/iPadOS where supported. Phase 1 is a standalone app; it does not require a DAW or commercial instruments.

**Website source:** [docs/index.html](docs/index.html). The repository is private and GitHub Pages is not enabled, so this source is not currently hosted.

## Phase 1 foundation

This repository contains a JUCE/CMake desktop app, background WAV/AIFF/FLAC scanning (subject to JUCE decoder support) with cancellation, a version-upgraded SQLite sample index, audio measurements, basic heuristic classification, category correction, user tags, similarity lookup, semantic drum-event and GM MIDI mapping, custom profile persistence/editing, and PPQ MIDI import/export. The desktop app discovers installed plugins, lets the user load an instrument and choose a kit, then scans MIDI notes 0–127 at selected velocities and capture settings. Non-silent previews and metadata are stored in SQLite and appear in the sample browser. The MagDrum VST3 loads user-selected audio samples from file or the shared SQLite index onto MIDI notes and saves sample paths with plugin state.

Sample-folder scans are recorded in history and use saved file size/modification time to skip unchanged files on later scans. An interrupted scan is marked on the next app launch; selecting the same folder again indexes missing or changed files.

The browser shows the selected sample waveform and can compare two selected waveforms side by side. Exact duplicates can be listed by matching file hashes. A separate near-duplicate search ranks feature-based candidates using duration, spectrum, crest factor, transient strength and confident pitch where available. It is a heuristic for listening review, not proof that two recordings sound identical.

See [the plugin scanning guide](docs/plugin-scanning.md) for the manual kit capture workflow and its current limits.

The current classifier is an early heuristic and the initial spectral measurement uses one analysis window. It is not a validated tagging model or perceptual fingerprint. Third-party plugin scanning still runs in-process, so a crashing plugin can terminate MagDrum. AU/AUv3 and iOS remain roadmap work.

## Build

Requires CMake 3.22+, C++20 and Git. JUCE and SQLite are fetched at pinned versions by CMake.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build --output-on-failure
```

See [roadmap](docs/roadmap.md) and [architecture](docs/architecture.md). The project license is undecided; keep this repository private until licensing is settled. No proprietary SDKs or copyrighted sample libraries are included.

See the current [semantic MIDI model](docs/midi-mapping.md) for its behavior and known mapping limitations.

The current locally verified package targets Windows x64 and contains the standalone app and VST3. The tagged workflow is configured for macOS VST3/AU and Linux VST3 archives, but GitHub Actions billing currently prevents those jobs from running. See the [install guide](docs/installing.md). Packages are unsigned test builds; AUv3/iOS and Intel macOS validation are not available yet.
