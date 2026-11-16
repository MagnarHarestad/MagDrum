# MagDrum local test package

Windows x64 test release: `v0.1.2-test.198` (VST3 version `0.1.2.198`).

- Windows x64 SHA-256: `4c642cffff13e72f671a08f70d58816302abcc38864022ef34763233110e60a2`
- Windows Release CTest: 2/2 passed, including live language switching across all eight languages in the main editor and both open auxiliary windows, plus cancellation of the installed-library drum-MIDI search from the VST; the standalone waveform refresh fix is compile-verified.
- The Windows ZIP includes a system-wide installer and a no-UAC per-user installer. The per-user installer can request one UAC approval for one-time legacy cleanup; subsequent per-user updates need no UAC.

See the [release package](https://github.com/MagnarHarestad/MagDrum/releases/tag/v0.1.2-test.198) and [Cubase test guide](../docs/cubase-testing.md). Linux and macOS builds remain paused until requested.
