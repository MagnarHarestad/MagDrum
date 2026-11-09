# MagDrum

MagDrum is a cross-platform drum sound library and MIDI mapping workspace. Browse WAV sample libraries alongside sounds from drum instruments, audition them, and transfer drum patterns through semantic MIDI mappings.

**MagDrum** is the product name. Formats are platform targets: VST3 on Windows, macOS and Linux; AU on macOS; AUv3 on iOS/iPadOS where supported. Phase 1 is a standalone app; it does not require a DAW or commercial instruments. Windows x64 is the current test build. Windows ARM64 is configured for cross-compilation but still needs its Visual Studio toolset and package verification.

**Website source:** [docs/index.html](docs/index.html). The repository is private and GitHub Pages is not enabled, so this source is not currently hosted.

## Phase 1 foundation

This repository contains a JUCE/CMake desktop app, background WAV/AIFF/FLAC scanning (subject to JUCE decoder support) with cancellation, a version-upgraded SQLite sample index, multi-window audio measurements, basic heuristic classification, category correction, user tags, similarity lookup, semantic drum-event and GM MIDI mapping, custom profile persistence/editing, and PPQ MIDI import/export. MIDI tempo and time signature can be edited at any beat without dropping other metadata. The editor reports ambiguous note assignments and compares the imported pattern with its current state, including events, tempo/meter and pattern length. The eight-lane step sequencer appends bars using the current meter when it fits the grid; right-click a step to set its velocity. It renders a tempo-aware preview from indexed drum samples and can loop it in the standalone app. The desktop app discovers installed plugins, lets the user load an instrument and choose a kit, then scans MIDI notes 0–127 at selected velocities, repeated takes and capture settings. Non-silent previews and metadata are stored in SQLite and appear in the sample browser; acoustic fingerprint changes between adjacent sampled velocities and repeated triggers are reported as heuristic candidates for listening review. The MagDrum VST3 loads user-selected audio samples from file or the shared SQLite index onto MIDI notes and saves sample paths with plugin state.

Sample-folder scans are recorded in history and use saved file size/modification time to skip unchanged files on later scans. An interrupted scan is marked on the next app launch; selecting the same folder again indexes missing or changed files.

The browser filters by sample folder, scanned instrument, kit, category, measured duration, spectral-brightness bands, classification confidence and text; it sorts by name, duration or brightness. A review filter shows uncorrected Unknown or below-75% heuristic classifications, and each row shows the confidence score or a manual marker. This threshold is a triage aid, not a calibrated probability. Plugin previews show their MIDI note, velocity and articulation in the shared list. It also shows the selected waveform and can compare two waveforms side by side. Exact duplicates can be listed by matching file hashes. A separate near-duplicate search compares normalized spectral fingerprints from four points in each sound, along with duration and audio features. Results are listening candidates, not proof that two recordings sound identical.

Select a captured plugin preview and choose **Open source kit** to reopen its instrument with the saved kit state, when the instrument is still installed. See [the plugin scanning guide](docs/plugin-scanning.md) for the manual kit capture workflow, VST3 preset loading and their current limits.

The classifier is an early heuristic. Its review filter surfaces Unknown and low-confidence results, but confidence has not been calibrated against a labeled sound corpus. Near-duplicate fingerprints are versioned and gain-normalized, but have not been evaluated against labeled examples. Plugin note scans run in a separate process to contain plugin crashes; a child-abort fixture exercises the process supervisor, while an end-to-end fixture with a real third-party instrument remains to be added. An initial iOS/AUv3 source target is present, but it still needs a Mac/Xcode build, signing and device testing.

## Build

Requires CMake 3.22+, C++20 and Git. JUCE and SQLite are fetched at pinned versions by CMake.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build --output-on-failure
```

On Windows, `pwsh -NoProfile -ExecutionPolicy Bypass -File .\Build-Windows.ps1` builds and tests the standalone app and VST3, then creates the Cubase ZIP under `latest-publish`. Add `-Architecture ARM64` to cross-compile for Windows ARM64 when its Visual Studio toolset is installed. `Build-Unix.sh` builds Linux x64 on Linux/WSL2 and macOS VST3/AU on a Mac. See [local platform builds](docs/local-builds.md) for requirements and limits.

See [roadmap](docs/roadmap.md) and [architecture](docs/architecture.md). The project license is undecided; keep this repository private until licensing is settled. No proprietary SDKs or copyrighted sample libraries are included.

See the current [semantic MIDI model](docs/midi-mapping.md) for its behavior and known mapping limitations.

The last published [Windows x64 test package v0.1.1-test.37](https://github.com/MagnarHarestad/MagDrum/releases/tag/v0.1.1-test.37) contains the standalone app, VST3 and PowerShell installer. A newer locally built package is available at `latest-publish/MagDrum-Windows-x64.zip` (SHA-256 `c235be49f50f9acb66fe1efd0cfc22e7733a6f3aa8c1278e342977f8ccf00258`) for direct Cubase testing without GitHub Actions or a release upload. GitHub Actions billing blocked all five hosted platform attempts before their steps started. Windows ARM64, both macOS targets and Linux x64 have not yet been packaged locally. See the [local build guide](docs/local-builds.md) and [install guide](docs/installing.md). Packages are unsigned test builds; the iOS/AUv3 source target has not yet been packaged.
