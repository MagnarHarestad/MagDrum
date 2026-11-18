# MagDrum Windows x64 test package

Release candidate: v0.1.2-test.232 (VST3 version 0.1.2.232).

- ZIP SHA-256: `e6f6f4a64a1a48fa36de68615d974af25afb43796016789c5466e2aa7d71194e`
- VST3 module SHA-256: `4417BF0E0C8054C9E50826652BF7C5992C2228A87B707C4F1610B0FBA3D7DC46`
- Windows x64 Release CTest: 2/2 passed, including per-pad compressor/phaser DSP and project-state persistence.
- Package verification passed for contents, license notices, Microsoft runtime signature and version.
- Per-pad compressor and four-stage phaser are selectable in the Drum Machine and fit the minimum editor size.
- Installed per-user at `%LOCALAPPDATA%\Programs\Common\VST3\MagDrum.vst3`; the previous .231 bundle is backed up outside VST3 scan paths.
- Installed module matches the release package hash and passed isolated scanning/rendering for MIDI notes 0–127, including C1.
- Cubase Pro 15 rescan and hands-on acceptance remain pending.

See the [release](https://github.com/MagnarHarestad/MagDrum/releases/tag/v0.1.2-test.232) and [Cubase test guide](../docs/cubase-testing.md). Linux and macOS remain paused until explicitly requested.