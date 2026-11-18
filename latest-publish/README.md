# MagDrum Windows x64 test package

Release candidate: v0.1.2-test.230 (VST3 version 0.1.2.230).

- ZIP SHA-256: `165d7b3e0867feb6c85bc039c466bb94e13b1065704e0afb978331b11f6d43b6`
- VST3 module SHA-256: `9f1e0772ffa3b778e97799321f1662a09ba18bae023a6ed098e553dd10ed6e5b`
- Windows x64 Release CTest: 2/2 passed, including 250,000-row sample map selection across browser pages.
- Package verification passed for contents, license notices, Microsoft runtime signature and version.
- The sample map now covers filtered libraries beyond the current list page; map selection supports preview and assignment.
- Sample-folder scans display 0–100% progress, completed file count and current filename.
- Installed per-user at `%LOCALAPPDATA%\Programs\Common\VST3\MagDrum.vst3`; the previous .229 bundle is backed up outside VST3 scan paths.
- Installed module matches the release package hash and passed isolated scanning/rendering for MIDI notes 0–127, including C1.
- Cubase Pro 15 rescan and hands-on acceptance remain pending.

See the [release](https://github.com/MagnarHarestad/MagDrum/releases/tag/v0.1.2-test.230) and [Cubase test guide](../docs/cubase-testing.md). Linux and macOS remain paused until explicitly requested.
