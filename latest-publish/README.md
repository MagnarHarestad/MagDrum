# Latest MagDrum packages

The local Windows x64 package is `MagDrum-Windows-x64.zip`. It contains the standalone app, VST3 instrument, and PowerShell installer for Cubase Pro 15 testing. It was published as [MagDrum v0.1.1-test.52](https://github.com/MagnarHarestad/MagDrum/releases/tag/v0.1.1-test.52), a locally built release that does not use GitHub Actions.

The desktop sample browser uses debounced background SQLite searches and fetches matching results in 250-sample pages, including text/tag, category, source/kit, feature, review, and sort filters. Sample scans persist each analyzed file immediately; plugin scans persist each completed capture so both can recover after interruption. Classifier-generated tags are stored separately from user tags, searchable, and shown in sample rows. The plugin scanner offers offline-fast, realtime-paced, and system audio-device callback modes. The VST3 sampler supports up to eight non-overlapping velocity layers per MIDI note. Both CTest suites passed (2/2), including a 50,000-row paging check, worker-crash recovery, realtime pacing, sample-rate metadata, virtual callback rendering, VST3 velocity-layer state checks, filename-boundary classifier checks, and classifier-tag persistence/search. The mobile shared-C++ preview also built and passed both suites (2/2); this does not verify iOS. Physical-device behavior still needs validation. The local archive SHA-256 is `ca9eb66d9ee3f9bbf8d0794b72d9674a57dd0d077c50a5d7f262036b132bbb13`.

The available local archive is Windows x64 only. Windows ARM64, macOS Intel, macOS Apple Silicon, Linux x64, and iOS/AUv3 packages have not been produced. See [local build requirements](../docs/local-builds.md).

Generated archives are ignored in this directory; this file is the checked-in package index.
