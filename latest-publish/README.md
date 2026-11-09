# Latest MagDrum packages

The current Windows x64 package is `MagDrum-Windows-x64.zip`, published as [MagDrum v0.1.1-test.47](https://github.com/MagnarHarestad/MagDrum/releases/tag/v0.1.1-test.47). It contains the standalone app, VST3 instrument, and PowerShell installer for Cubase Pro 15 testing.

The desktop sample browser uses debounced background SQLite searches and fetches matching results in 250-sample pages, including text/tag, category, source/kit, feature, review, and sort filters. Sample scans persist each analyzed file immediately; plugin scans persist each completed capture so both can recover after interruption. The plugin scanner offers offline-fast, realtime-paced, and system audio-device callback modes. Both CTest suites passed, including a 50,000-row paging check, worker-crash recovery, realtime pacing, sample-rate metadata and virtual callback-rendering checks. Physical-device behavior still needs validation. The archive SHA-256 is `460345fbdfbba14420eb9acefdffc8a85ae9055c31ebcb63eb7ec23187c47e96`.

The available local archive is Windows x64 only. Windows ARM64, macOS Intel, macOS Apple Silicon, Linux x64, and iOS/AUv3 packages have not been produced. See [local build requirements](../docs/local-builds.md).

Generated archives are ignored in this directory; this file is the checked-in package index.
