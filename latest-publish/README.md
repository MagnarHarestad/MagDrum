# MagDrum local test package

Windows x64 test release: v0.1.2-test.209 (VST3 version 0.1.2.209).

- Windows x64 ZIP SHA-256: 50e6b869edbce793b2c81c87255e25fd62031c648ad7c3c1a3feedf28ea7ca5e
- Packaged VST3 module SHA-256: ECB8F23482786050B3CCE7F7B08620CB6B0DF2464F1699C8301E5C0F992187BB
- Release CTest: 2/2 passed.
- Windows package verifier passed for contents, license notices, signatures and version 0.1.2.209.
- Steinberg VST3 SDK validator: 47/47 checks passed; 1 VST parameter exposed, MIDI CC uses the VST3 event bus.
- MIDI importer coverage from this build includes tempo-map conversion, 29.97 fps drop-frame timing and invalid SMPTE rate rejection.
- Install and Cubase Pro 15 acceptance are pending.

See the [Windows release package](https://github.com/MagnarHarestad/MagDrum/releases/tag/v0.1.2-test.209) and [Cubase test guide](../docs/cubase-testing.md). Linux and macOS remain paused until explicitly requested.
