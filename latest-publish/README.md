# MagDrum Windows x64 test package

Release candidate: v0.1.2-test.220 (VST3 version 0.1.2.220).

- ZIP SHA-256: `56c44554607de4a37477862b6af58736397b65f63a0faca048900d9baf435cc2`
- VST3 module SHA-256: `8f7b1a186438a25d9c742f2f952464ecd4710a6f21d5bad5bcf388fa3053b0dc`
- Windows x64 Release CTest: 2/2 passed.
- Package verification passed for contents, license notices, signatures and version.
- Exact packaged VST3 passed isolated scanner/render for MIDI notes 0–127, including C1.
- Installed per-user at `%LOCALAPPDATA%\Programs\Common\VST3\MagDrum.vst3`; installed module hash matches the package.
- Previous .219 bundle preserved outside VST3 scan paths at `%LOCALAPPDATA%\MagDrum\VST3-backups\MagDrum.vst3.backup-20261118-072136-c18eb058494b469988160c0d38286ad4`.
- Cubase Pro 15 rescan and hands-on acceptance remain pending.

See the [release](https://github.com/MagnarHarestad/MagDrum/releases/tag/v0.1.2-test.220) and [Cubase test guide](../docs/cubase-testing.md). Linux and macOS remain paused until explicitly requested.
