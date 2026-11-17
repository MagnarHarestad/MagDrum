# MagDrum local test package

Windows x64 test release: v0.1.2-test.208 (VST3 version 0.1.2.208).

- Windows x64 ZIP SHA-256: 15c43b053c6200143dbcc82ba8f3e582cd10547bc669786084c5a880a242ea7c
- Packaged VST3 module SHA-256: 2f3d8ccbe3fcc17fae08ea88a6a82b948c4f48ab22276c2fc3d0833923c6785c
- Windows Release CTest: 2/2 passed. The core tests cover tempo-map conversion, 29.97 fps drop-frame timing and invalid SMPTE rates; the VST editor test imports a SMPTE file into the sequencer.
- The package checker verified contents, license notices, signatures and version 0.1.2.208.

The MIDI importer accepts PPQ and valid SMPTE divisions at 24, 25, 29.97 or 30 fps. It converts absolute time to musical beats using the tempo map, defaulting to 120 BPM before a tempo event, and exports in PPQ format.

See the [Windows release package](https://github.com/MagnarHarestad/MagDrum/releases/tag/v0.1.2-test.208) and [Cubase test guide](../docs/cubase-testing.md). Linux and macOS remain paused until explicitly requested.