# MagDrum Windows x64 test package

Public Windows x64 prerelease: [v0.1.2-test.296](https://github.com/MagnarHarestad/MagDrum/releases/tag/v0.1.2-test.296) (VST3 version 0.1.2.296).

- ZIP SHA-256: eb8d69ccac7515790923c73cf0490d4d5f94386cc7c912c76a0f5bdd058a70e8
- VST3 module SHA-256: 16348B3FA189A0584580790F269FED190447D2AC4E95810BC27B6BBC58F935D3
- Windows x64 Release CTest: 8 applicable tests passed; the Debug-only assertion worker was skipped.
- Package contents, license notices, signatures and version checks passed.
- The exact ZIP passed saved-state restore and MIDI rendering for notes 0–127, including C1.
- Twelve offscreen UI images were rendered from the packaged VST3.
- Steinberg VST 3.8.1 validation passed 47/47 standard and 537/537 extensive tests.
- No-program plugin kit guidance is now localized in all eight languages in the VST and standalone manager.
- The plugin has not been installed; .289 remains the per-user installation. Real Cubase-host behavior remains unverified.

See [the Cubase test guide](../docs/cubase-testing.md).
