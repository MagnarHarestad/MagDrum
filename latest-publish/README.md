# MagDrum Windows x64 test package

Public prerelease: [v0.1.2-test.264](https://github.com/MagnarHarestad/MagDrum/releases/tag/v0.1.2-test.264) (VST3 version 0.1.2.264).

- ZIP SHA-256: `a54cad91d150d81d6b289d2ba33bcbf03a5a179cd3288ec1276ea835645295ce`
- VST3 module SHA-256: `1C510189030C1D2F1E453155D4C36F837961B0287B7617D0CA16D5743E36269F`
- Windows x64 Release CTest: 4/4 passed; the Debug-only assertion-worker test is skipped in Release.
- Package contents, license notices, Visual C++ runtime signature and product version passed verification.
- The exact packaged VST3 passed saved-state restoration and isolated MIDI 0–127 rendering, including C1.
- Steinberg VST 3.8.1 validation: 47/47 standard and 537/537 extended tests passed.
- The Plugin Scanner groups program and preset actions under **Choose kit source…**; Sample Library loads more results while scrolling.
- The Sequencer groups tempo/meter event changes under one **Event actions...** menu.
- Ten Release offscreen images cover all five pages, the in-app MIDI editor and picker, and compact effect-panel layouts.
- The .261 package is published but not installed; .252 remains installed per-user until the Windows feature work is complete.
- Cubase Pro 15 interaction, device audio and project save/reopen remain unverified.

See the [release](https://github.com/MagnarHarestad/MagDrum/releases/tag/v0.1.2-test.264) and [Cubase test guide](../docs/cubase-testing.md).
Linux and macOS remain paused until explicitly requested.
