# MagDrum local test packages

Release [v0.1.1-test.134](https://github.com/MagnarHarestad/MagDrum/releases/tag/v0.1.1-test.134) contains Windows x64 and Linux x64 builds. The sequencer grid now follows MIDI time-signature changes placed on sixteenth-note boundaries, including partial bars. Both Release CTest suites passed on each platform (2/2).

- Windows x64 SHA-256: `81c5b1023c925b1b580d9936cd9c74dd2cc565a3368de030e02deab0b5705e6b`
- Linux x64 SHA-256: `34a22a809dfe46c570477777a1ea39c99dd4cc2697fb2bac0fcae577719903f6`

MagDrum v0.1.1-test.133 remains installed in the standard Windows VST3 folder until the Cubase process fully exits. Windows still reports a background `Cubase15` process after its window was closed, and access to its loaded-module list was denied. Once Cubase is fully closed, replace the existing `MagDrum.vst3` with this release, then rescan in Cubase. The older .131 bundle is backed up under `%LOCALAPPDATA%\\MagDrum\\VST3-backups`.
