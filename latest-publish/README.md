# MagDrum local test package

Windows x64 test release: `v0.1.2-test.199` (VST3 version `0.1.2.199`).

- Windows x64 SHA-256: `02e3f9fae9e916ed3ba749ad7399552ff20479707c0b69f5dddd39d50e7c5654`
- Windows Release CTest: 2/2 passed, including live language switching across all eight languages in the main editor and both open auxiliary windows, plus cancellation of installed-library drum-MIDI search from the VST and Plugin Scanner folder-button visibility across tab switches; the standalone waveform refresh fix is compile-verified.
- The Windows ZIP includes a system-wide installer and a no-UAC per-user installer. The per-user installer can request one UAC approval for one-time legacy cleanup; subsequent per-user updates need no UAC.

See the [release package](https://github.com/MagnarHarestad/MagDrum/releases/tag/v0.1.2-test.199) and [Cubase test guide](../docs/cubase-testing.md). Linux and macOS builds remain paused until requested.
