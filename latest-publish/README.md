# MagDrum local test package

Windows x64 test release: `v0.1.2-test.197` (VST3 version `0.1.2.197`).

- Windows x64 SHA-256: `d2041e405b8da9b2a41d6930e035754aaf1aa4c07850c770ee33699d1d06d01f`
- Windows Release CTest: 2/2 passed, including live language switching across all eight languages in the main editor and both open auxiliary windows, plus cancellation of the installed-library drum-MIDI search from the VST; the standalone waveform refresh fix is compile-verified.
- The Windows ZIP includes the double-click UAC installer launcher; it confirms replacement, preserves the old VST3 in backup and leaves errors visible.

See the [release package](https://github.com/MagnarHarestad/MagDrum/releases/tag/v0.1.2-test.197) and [Cubase test guide](../docs/cubase-testing.md). Linux and macOS builds remain paused until requested.
