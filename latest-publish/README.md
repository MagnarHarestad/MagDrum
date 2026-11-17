# MagDrum local test package

Windows x64 test release: `v0.1.2-test.206` (VST3 version `0.1.2.206`).

- Windows x64 SHA-256: `354fac2556ab84f48f50b432e33407ac5ad5ed84b58d05a9e4a694419de028e7`
- Windows Release CTest: 2/2 passed. The .203 build adds a conservative low-confidence kick suggestion. On the 52-file CC0 three-kit corpus, audio-only accuracy increased from 32.7% to 44.2% / macro F1 0.320; three percussion samples were false kick suggestions and 0/52 were accepted at 0.75. The 121-file single-kit audio-only result remained 22.3% / 0.212.
- The sample-library view projects four-window, 16-band fingerprints onto two principal components in a background worker. Libraries over 3,000 loaded samples show an even representative map subset; the full result list remains available.
- The installed VST3 module hash `f9406d5508e0d37e18c2117267f7e4e6b5ef9b9739833840f086671209e163f7` matches the module in the Windows ZIP. Release CTest passed 2/2 and the isolated scanner/render test passed against the installed .206 module. The Windows ZIP includes a system-wide installer and a no-UAC per-user installer. The per-user installer can request one UAC approval for one-time legacy cleanup; subsequent per-user updates need no UAC.

See the [release package](https://github.com/MagnarHarestad/MagDrum/releases/tag/v0.1.2-test.206) and [Cubase test guide](../docs/cubase-testing.md). Linux and macOS builds remain paused until requested.
