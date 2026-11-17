# MagDrum local test package

Windows x64 test release: `v0.1.2-test.202` (VST3 version `0.1.2.202`).

- Windows x64 SHA-256: `7321b5a774c572dc6e66a7ac8db26564847a764ed01b50fd4835da641c0b5637`
- Windows Release CTest: 2/2 passed. The .202 build adds explicit percussion instrument name recognition. In the 121-file CC0 single-kit evaluation, filename-plus-audio reached 121/121 correct; filename-hidden audio-only accuracy remained 22.3%.
- The installed VST3 module hash `9a8dfe278e75738fd1fc38d17e591a9d03c6e86c0aa82ee8e3b848dccb92bb11` matches the module in the Windows ZIP. Release CTest passed 2/2 and the isolated scanner/render test passed against the installed .202 module. The Windows ZIP includes a system-wide installer and a no-UAC per-user installer. The per-user installer can request one UAC approval for one-time legacy cleanup; subsequent per-user updates need no UAC.

See the [release package](https://github.com/MagnarHarestad/MagDrum/releases/tag/v0.1.2-test.202) and [Cubase test guide](../docs/cubase-testing.md). Linux and macOS builds remain paused until requested.
