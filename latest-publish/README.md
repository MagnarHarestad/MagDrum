# MagDrum Windows x64 test package

Verified Windows x64 prerelease candidate: v0.1.2-test.294 (VST3 version 0.1.2.294).

- ZIP SHA-256: `7eb211a246479468bc223686e4ce8710afdc62fd69f4c8c1b27b630e532f6cd3`
- VST3 module SHA-256: `05d4a50f0368f6f9ecbf56671c84939c910e3449bed63e5e0a5c3f5b50d12c46`
- Windows x64 Release CTest: 8 applicable tests passed; the Debug-only assertion worker was skipped.
- Package contents, license notices, signatures and version checks passed.
- The exact ZIP passed saved-state restore and MIDI rendering for all notes 0–127, including C1.
- Twelve offscreen UI images were rendered from the packaged VST3.
- Steinberg VST 3.8.1 validation passed 47/47 standard and 537/537 extensive tests.
- MIDI import preserves unmapped raw pitches; drum-family filters, acoustic-map zoom/pan and Battery-kit parser safeguards are included.
- The plugin has not been installed; .289 remains the per-user installation. Real Cubase-host behavior remains unverified.

See [the Cubase test guide](../docs/cubase-testing.md).
