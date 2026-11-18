# MagDrum Windows x64 test package

Release candidate: v0.1.2-test.221 (VST3 version 0.1.2.221).

- ZIP SHA-256: `4c78bd14b03a7823df69ac8e3e0348f449a7aa4c19ee0b3530c18bfb3370cc09`
- VST3 module SHA-256: `69402966eeafa0cab24c9be0eb59454d9420628de5b3a75b0e9e80a915baba38`
- Windows x64 Release CTest: 2/2 passed, including all 16 sequencer lane labels and General MIDI note assignments.
- Package verification passed for contents, license notices, Microsoft runtime signature and version.
- Installed VST3 matches the package hash and passed isolated scanner/render for MIDI notes 0–127, including C1.
- Installed per-user at `%LOCALAPPDATA%\Programs\Common\VST3\MagDrum.vst3`.
- Previous .220 bundle preserved outside VST3 scan paths under `%LOCALAPPDATA%\MagDrum\VST3-backups`.
- Cubase Pro 15 rescan and hands-on acceptance remain pending.

See the [release](https://github.com/MagnarHarestad/MagDrum/releases/tag/v0.1.2-test.221) and [Cubase test guide](../docs/cubase-testing.md). Linux and macOS remain paused until explicitly requested.
