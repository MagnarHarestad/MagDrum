# MagDrum Windows x64 test package

Public prerelease: [v0.1.2-test.287](https://github.com/MagnarHarestad/MagDrum/releases/tag/v0.1.2-test.287) (VST3 version 0.1.2.287).

- ZIP SHA-256: `13761b0eb742c8c54a433e8adf7ea074b4b2484bd075eb424bbfa4ac8f5bbcc8`
- VST3 module SHA-256: `9C8E8E99456C7ABDF6BB6953FA3A7BB10E6C1A1CC47218F28745685B7227BFB3`
- Windows x64 Release CTest: 10/10 passed; the Debug-only assertion worker was skipped.
- Package contents, license notices, signatures and version checks passed.
- The package passed project-state restore and isolated MIDI rendering for notes 0–127, including C1.
- Twelve offscreen UI images were rendered. Steinberg VST 3.8.1 validation passed 47 standard and 537 extensive checks.
- The scanner page now starts/cancels instrument discovery directly; custom folders and inventory refresh are in the app menu.
- Published but not installed. Manual Cubase-host interaction remains unverified.

See [the Cubase test guide](../docs/cubase-testing.md).
