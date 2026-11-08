# MagDrum

MagDrum is a cross-platform drum sound library and MIDI mapping workspace. Browse WAV sample libraries alongside sounds from drum instruments, audition them, and transfer drum patterns through semantic MIDI mappings.

**MagDrum** is the product name. Formats are platform targets: VST3 on Windows, macOS and Linux; AU on macOS; AUv3 on iOS/iPadOS where supported. Phase 1 is a standalone app; it does not require a DAW or commercial instruments. Windows x64 is the current test build; Windows ARM64 is now included in CI/package configuration but remains unverified.

**Website source:** [docs/index.html](docs/index.html). The repository is private and GitHub Pages is not enabled, so this source is not currently hosted.

## Phase 1 foundation

This repository contains a JUCE/CMake desktop app, background WAV/AIFF/FLAC scanning (subject to JUCE decoder support) with cancellation, a version-upgraded SQLite sample index, audio measurements, basic heuristic classification, category correction, user tags, similarity lookup, semantic drum-event and GM MIDI mapping, custom profile persistence/editing, and PPQ MIDI import/export. MIDI tempo and time signature can be edited at the pattern start without dropping later changes. The eight-lane step sequencer appends bars using the current meter when it fits the grid; right-click a step to set its velocity. The desktop app discovers installed plugins, lets the user load an instrument and choose a kit, then scans MIDI notes 0–127 at selected velocities and capture settings. Non-silent previews and metadata are stored in SQLite and appear in the sample browser. The MagDrum VST3 loads user-selected audio samples from file or the shared SQLite index onto MIDI notes and saves sample paths with plugin state.

Sample-folder scans are recorded in history and use saved file size/modification time to skip unchanged files on later scans. An interrupted scan is marked on the next app launch; selecting the same folder again indexes missing or changed files.

The browser filters by sample folder, scanned instrument, kit, category and text. Plugin previews show their MIDI note, velocity and articulation in the shared list. It also shows the selected waveform and can compare two waveforms side by side. Exact duplicates can be listed by matching file hashes. A separate near-duplicate search compares normalized spectral fingerprints from four points in each sound, along with duration and audio features. Results are listening candidates, not proof that two recordings sound identical.

See [the plugin scanning guide](docs/plugin-scanning.md) for the manual kit capture workflow and its current limits.

The current classifier is an early heuristic and the initial spectral measurement uses one analysis window. Near-duplicate fingerprints are versioned and gain-normalized, but have not been evaluated against a labeled sound corpus. Third-party plugin scanning still runs in-process, so a crashing plugin can terminate MagDrum. AUv3 and iOS remain roadmap work.

## Build

Requires CMake 3.22+, C++20 and Git. JUCE and SQLite are fetched at pinned versions by CMake.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build --output-on-failure
```

On Windows, `pwsh -NoProfile -ExecutionPolicy Bypass -File .\Build-Windows.ps1` builds and tests the standalone app and VST3, then creates the Cubase ZIP under `latest-publish`. See [local platform build limits](docs/local-builds.md) for ARM64, Linux, macOS, and AUv3 requirements.

See [roadmap](docs/roadmap.md) and [architecture](docs/architecture.md). The project license is undecided; keep this repository private until licensing is settled. No proprietary SDKs or copyrighted sample libraries are included.

See the current [semantic MIDI model](docs/midi-mapping.md) for its behavior and known mapping limitations.

The current locally verified package targets Windows x64 and contains the standalone app and VST3. The release workflow configures Windows ARM64, separate Intel and Apple Silicon macOS packages with VST3/AU, and Linux x64, but GitHub Actions billing currently prevents hosted jobs from running. See the [install guide](docs/installing.md). Packages are unsigned test builds; AUv3/iOS is not available yet.
