# MagDrum Windows x64 test package

Staged Windows x64 prerelease: `v0.1.2-test.288` (VST3 version 0.1.2.288). Release preflight and publication are pending.

- ZIP SHA-256: `70d263fb32922530ddbb5caf65194527c6d4f5c722e5e7c7f084586a5d45f2ad`
- VST3 module SHA-256: `C149EE32D8C60E264740FB6912AE817291FFF2BF08C7F08B7CBEEB50494E3E50`
- Windows x64 Release CTest: 9 applicable tests passed; the Debug-only assertion worker was skipped.
- Package contents, license notices, signatures and version checks passed.
- The package passed project-state restore and isolated MIDI rendering for notes 0–127, including C1.
- The repeat VST3 scan directory cache reuses unchanged folders and invalidates added or removed bundles.
- Release preflight for offscreen UI, project-state restore, MIDI rendering, and Steinberg VST 3.8.1 validation is pending.
- The scanner page now starts/cancels instrument discovery directly; custom folders and inventory refresh are in the app menu.
- Staged but not installed. Manual Cubase-host interaction remains unverified.

See [the Cubase test guide](../docs/cubase-testing.md).
