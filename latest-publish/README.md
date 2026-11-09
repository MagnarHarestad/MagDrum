# Latest MagDrum packages

The local Windows x64 package is `MagDrum-Windows-x64.zip`. It contains the standalone app, VST3 instrument, and PowerShell installer for Cubase Pro 15 testing. The published [MagDrum v0.1.1-test.50](https://github.com/MagnarHarestad/MagDrum/releases/tag/v0.1.1-test.50) matches this locally built archive; the earlier [v0.1.1-test.49](https://github.com/MagnarHarestad/MagDrum/releases/tag/v0.1.1-test.49) is also available on GitHub.

The desktop sample browser uses debounced background SQLite searches and fetches matching results in 250-sample pages, including text/tag, category, source/kit, feature, review, and sort filters. Sample scans persist each analyzed file immediately; plugin scans persist each completed capture so both can recover after interruption. The plugin scanner offers offline-fast, realtime-paced, and system audio-device callback modes. The VST3 sampler supports up to eight non-overlapping velocity layers per MIDI note. Both CTest suites passed, including a 50,000-row paging check, worker-crash recovery, realtime pacing, sample-rate metadata, virtual callback rendering and VST3 velocity-layer state checks. Physical-device behavior still needs validation. The local archive SHA-256 is `23f18c977e937f2621e1477765409dd7b65908eb08610f08ace16f477d8fcb5e`.

The available local archive is Windows x64 only. Windows ARM64, macOS Intel, macOS Apple Silicon, Linux x64, and iOS/AUv3 packages have not been produced. See [local build requirements](../docs/local-builds.md).

Generated archives are ignored in this directory; this file is the checked-in package index.
