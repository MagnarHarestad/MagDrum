# MagDrum local test package

Windows x64 test release: `v0.1.2-test.203` (VST3 version `0.1.2.203`).

- Windows x64 SHA-256: `d3d21e47a163ae51cc32dbbb7b30f4b7f968abb25c24a0748804e2f1368c50a9`
- Windows Release CTest: 2/2 passed. The .203 build adds a conservative low-confidence kick suggestion. On the 52-file CC0 three-kit corpus, audio-only accuracy increased from 32.7% to 44.2% / macro F1 0.320; three percussion samples were false kick suggestions and 0/52 were accepted at 0.75. The 121-file single-kit audio-only result remained 22.3% / 0.212.
- The installed VST3 module hash `198dcda4cc0fb73d8fd2d8f66121b76fad1d8974541ca99f4f78d41fda056504` matches the module in the Windows ZIP. Release CTest passed 2/2 and the isolated scanner/render test passed against the installed .203 module. The Windows ZIP includes a system-wide installer and a no-UAC per-user installer. The per-user installer can request one UAC approval for one-time legacy cleanup; subsequent per-user updates need no UAC.

See the [release package](https://github.com/MagnarHarestad/MagDrum/releases/tag/v0.1.2-test.203) and [Cubase test guide](../docs/cubase-testing.md). Linux and macOS builds remain paused until requested.
