# MagDrum Windows x64 test package

Release candidate: v0.1.2-test.223 (VST3 version 0.1.2.223).

- ZIP SHA-256: `7470fda308939e49d921d4d8b080503f59c879428a267051ee5ade99caffe23e`
- VST3 module SHA-256: `4D266231D479B438AC9E1883D637692D7B95F9D489988B90C272984BE39FB11C`
- Windows x64 Release CTest: 2/2 passed, including all 16 sequencer lane labels and MIDI notes, plus vertical alignment checks for all VST text fields.
- Package verification passed for contents, license notices, Microsoft runtime signature and version.
- Sample-folder scans display 0–100% progress, completed file count and current filename.
- Installed VST3 matches the package hash and passed isolated scanner/render for MIDI notes 0–127, including C1.
- Installed per-user at `%LOCALAPPDATA%\Programs\Common\VST3\MagDrum.vst3`.
- Previous .222 bundle preserved outside VST3 scan paths under `%LOCALAPPDATA%\MagDrum\VST3-backups`.
- Cubase Pro 15 rescan and hands-on acceptance remain pending.

See the [release](https://github.com/MagnarHarestad/MagDrum/releases/tag/v0.1.2-test.223) and [Cubase test guide](../docs/cubase-testing.md). Linux and macOS remain paused until explicitly requested.
