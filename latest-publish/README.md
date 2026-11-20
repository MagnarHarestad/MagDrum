# MagDrum Windows x64 test package

Public Windows x64 prerelease: [v0.1.2-test.295](https://github.com/MagnarHarestad/MagDrum/releases/tag/v0.1.2-test.295) (VST3 version 0.1.2.295).

- ZIP SHA-256: `455ca5e9c3ec0d6e22f1ea79e116603238ef28582e5b317d093b827f6376d24c`
- VST3 module SHA-256: `21913af181c2c702eb7c2b93c8b16a0cf45d6a114a75832c55b10014910d468d`
- Windows x64 Release CTest: 8 applicable tests passed; the Debug-only assertion worker was skipped.
- Package contents, license notices, signatures and version checks passed.
- The exact ZIP passed saved-state restore and MIDI rendering for all notes 0–127, including C1.
- Twelve offscreen UI images were rendered from the packaged VST3.
- Steinberg VST 3.8.1 validation passed 47/47 standard and 537/537 extensive tests.
- Successful plugin discovery with no instrument results now gives localized guidance in the status line.
- The plugin has not been installed; .289 remains the per-user installation. Real Cubase-host behavior remains unverified.

See [the Cubase test guide](../docs/cubase-testing.md).
