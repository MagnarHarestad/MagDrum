# MagDrum Windows x64 test package

Release candidate: v0.1.2-test.222 (VST3 version 0.1.2.222).

- ZIP SHA-256: `a2b41292d3892d8c483d1440f80a1c0a9e4f3d84f788e55881b46682d37528c2`
- VST3 module SHA-256: `6b4dfffe5b08dbda1c6bfdc5867d13e5d7a0000372f0cafa46a52fd071c8e351`
- Windows x64 Release CTest: 2/2 passed, including all 16 sequencer lane labels and MIDI notes, plus vertical alignment checks for all VST text fields.
- Package verification passed for contents, license notices, Microsoft runtime signature and version.
- Installed VST3 matches the package hash and passed isolated scanner/render for MIDI notes 0–127, including C1.
- Installed per-user at `%LOCALAPPDATA%\Programs\Common\VST3\MagDrum.vst3`.
- Previous .220 bundle preserved outside VST3 scan paths under `%LOCALAPPDATA%\MagDrum\VST3-backups`.
- Previous .221 bundle preserved outside VST3 scan paths under `%LOCALAPPDATA%\MagDrum\VST3-backups`.
- Cubase Pro 15 rescan and hands-on acceptance remain pending.

See the [release](https://github.com/MagnarHarestad/MagDrum/releases/tag/v0.1.2-test.222) and [Cubase test guide](../docs/cubase-testing.md). Linux and macOS remain paused until explicitly requested.
