# MagDrum Windows x64 test package

Public prerelease: [v0.1.2-test.275](https://github.com/MagnarHarestad/MagDrum/releases/tag/v0.1.2-test.275) (VST3 version 0.1.2.275).

- ZIP SHA-256: `0163BCEB386993E862497273358764C4BD5C8BBB0A0852EAEEC9B2E02D0CFE7A`
- VST3 module SHA-256: `C0319107CD9C9534AFF8238A786F192BF4700B6C19694734F0B0C8E0CA8E9B9A`
- Windows x64 Release CTest: 5 applicable tests passed; the Debug-only assertion worker was skipped.
- Localization validation: 629 source labels present in all seven non-English catalogs.
- Exact package passed project-state restore and isolated MIDI rendering for notes 0–127, including C1.
- Instrument manager saves now refresh the VST kit inventory and sample browser automatically when the manager closes; covered by an offscreen workflow test.
- This release is published but not installed; .274 remains installed until Windows feature work is complete.
- Real Cubase interaction, populated Battery kit capture and direct `.nbkt` import remain unverified.

See [the Cubase test guide](../docs/cubase-testing.md).
