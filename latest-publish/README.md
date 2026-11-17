# MagDrum local test package

Windows x64 test release: `v0.1.2-test.204` (VST3 version `0.1.2.204`).

- Windows x64 SHA-256: `70cdb10004390a0690f9eba2605994a234f0e616008370056125583c64317581`
- Windows Release CTest: 2/2 passed. The .203 build adds a conservative low-confidence kick suggestion. On the 52-file CC0 three-kit corpus, audio-only accuracy increased from 32.7% to 44.2% / macro F1 0.320; three percussion samples were false kick suggestions and 0/52 were accepted at 0.75. The 121-file single-kit audio-only result remained 22.3% / 0.212.
- The sample-library view now projects stored four-window, 16-band fingerprints onto two principal components so similar spectra tend to appear nearby. Samples without fingerprints use duration and brightness for an estimated position.
- The installed VST3 module hash `dd1d22a052eae36687440a63e177b757cfeb680cd8988b98d60533216c812e1c` matches the module in the Windows ZIP. Release CTest passed 2/2 and the isolated scanner/render test passed against the installed .204 module. The Windows ZIP includes a system-wide installer and a no-UAC per-user installer. The per-user installer can request one UAC approval for one-time legacy cleanup; subsequent per-user updates need no UAC.

See the [release package](https://github.com/MagnarHarestad/MagDrum/releases/tag/v0.1.2-test.204) and [Cubase test guide](../docs/cubase-testing.md). Linux and macOS builds remain paused until requested.
