# MagDrum Windows x64 test package

Public prerelease: [v0.1.2-test.284](https://github.com/MagnarHarestad/MagDrum/releases/tag/v0.1.2-test.284) (VST3 version 0.1.2.284).

- ZIP SHA-256: `88448f76a2350017cc501ad398a080935b98d43d21552b5357e05f35732b8c9a`
- VST3 module SHA-256: `69ADFF5C6F02656AC4C3D41C0F06E0F1BDE609E37235A501B748F9FB9E7998D2`
- Windows x64 Release CTest: 6 applicable tests passed; the Debug-only assertion worker was skipped.
- Localization and package validation passed. Steinberg VST 3.8.1 validation passed 47 standard and 537 extended checks.
- The package passed project-state restore and isolated MIDI rendering for notes 0–127, including C1.
- Offscreen direct `.nbkt` imports passed for three installed Battery factory kits, including overlapping velocity layers.
- This release is installed per user; `.281` is preserved outside VST3 scan paths.
- Manual Cubase-host interaction remains unverified.

See [the Cubase test guide](../docs/cubase-testing.md).
