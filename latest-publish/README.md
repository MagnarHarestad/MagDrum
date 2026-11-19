# MagDrum Windows x64 test package

Windows test package: v0.1.2-test.265 (VST3 version 0.1.2.265); publish is pending.

- ZIP SHA-256: `ad9426150f12fe3a194ff194a7cb136e36fe247199679d7b8168160f397c379a`
- VST3 module SHA-256: `85C47DFBE6AA8D95F20C6060D3555B1E2DAF67491D7C1DBF4D3979199211929F`
- Windows x64 Release CTest: 4/4 passed; the Debug-only assertion-worker test is skipped in Release.
- Package contents, license notices, Visual C++ runtime signature and product version passed verification.
- The exact packaged VST3 passed saved-state restoration and isolated MIDI 0–127 rendering, including C1.
- Steinberg VST 3.8.1 validation: 47/47 standard and 537/537 extended tests passed.
- The Sample Library keeps sound family and category visible, with secondary filters grouped under **More filters** and an active-filter count.
- The Plugin Scanner groups program and preset actions under **Choose kit source…**; Sample Library loads more results while scrolling.
- The Sequencer groups tempo/meter event changes under one **Event actions...** menu.
- Eleven Release offscreen images cover all five pages, both Sample Library filter states, the in-app MIDI editor and picker, and compact effect-panel layouts.
- This package has not been installed; .252 remains installed per-user until Windows feature work is complete.
- Cubase Pro 15 interaction, device audio and project save/reopen remain unverified.

When published, find the package at [release v0.1.2-test.265](https://github.com/MagnarHarestad/MagDrum/releases/tag/v0.1.2-test.265); see the [Cubase test guide](../docs/cubase-testing.md).
Linux and macOS remain paused until explicitly requested.
