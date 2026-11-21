# MagDrum Windows x64 test package

Public Windows x64 prerelease: [v0.1.2-test.300](https://github.com/MagnarHarestad/MagDrum/releases/tag/v0.1.2-test.300) (VST3 version 0.1.2.300).

- ZIP SHA-256: 0c4a381459c13487259b31fc9397258a3d12803bcd04e38afa272b8864589b7a
- VST3 module SHA-256: 70C04B25DA22D684E163D22137F299EA31EBC5F1535CE8BD8F5245D770CDD68E
- Windows x64 Release CTest: 8 applicable tests passed; the Debug-only assertion worker was skipped.
- Package contents, license notices, signatures and version checks passed.
- The exact ZIP passed saved-state restore and MIDI rendering for notes 0–127, including C1.
- Twelve offscreen UI images were rendered from the packaged VST3.
- Steinberg VST 3.8.1 validation passed 47/47 standard and 537/537 extensive tests.
- No-program plugin kit guidance is now localized in all eight languages in the VST and standalone manager.
- The .300 module is installed per-user; .299 is preserved outside VST3 scan paths. Real Cubase-host behavior remains unverified.

See [the Cubase test guide](../docs/cubase-testing.md).
