# MagDrum Windows x64 test package

Public prerelease: [v0.1.2-test.276](https://github.com/MagnarHarestad/MagDrum/releases/tag/v0.1.2-test.276) (VST3 version 0.1.2.276).

- ZIP SHA-256: `8806a388ba655c7b779ba965595573cfc41eccf83136aed76f6450dc04216e4d`
- VST3 module SHA-256: `4E71BD076684E0F5BA6589344D3EB3B5544A90008A68A61F57550BACB40D88C0`
- Windows x64 Release CTest: 5 applicable tests passed; the Debug-only assertion worker was skipped.
- Localization validation: 629 source labels present in all seven non-English catalogs.
- Exact package passed project-state restore and isolated MIDI rendering for notes 0–127, including C1.
- Battery and generic-instrument kit guidance now matches automatic refresh when the manager closes.
- This release is published but not installed; .274 remains installed until Windows feature work is complete.
- Real Cubase interaction, populated Battery kit capture and direct `.nbkt` import remain unverified.

See [the Cubase test guide](../docs/cubase-testing.md).