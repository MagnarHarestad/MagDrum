# MagDrum local test packages

Release [v0.1.1-test.134](https://github.com/MagnarHarestad/MagDrum/releases/tag/v0.1.1-test.134) contains Windows x64 and Linux x64 builds. The sequencer grid now follows MIDI time-signature changes placed on sixteenth-note boundaries, including partial bars. Both Release CTest suites passed on each platform (2/2).

- Windows x64 SHA-256: `81c5b1023c925b1b580d9936cd9c74dd2cc565a3368de030e02deab0b5705e6b`
- Linux x64 SHA-256: `34a22a809dfe46c570477777a1ea39c99dd4cc2697fb2bac0fcae577719903f6`

MagDrum v0.1.1-test.134 is installed in `C:\\Program Files\\Common Files\\VST3\\MagDrum.vst3`. The previous .133 bundle is backed up under `%ProgramData%\\MagDrum\\VST3-backups`, outside Cubase's scan folder. Windows confirmed that only one MagDrum bundle remains in the system VST3 folder and that the background Cubase15 process has no MagDrum module loaded. Restart Cubase and choose **Studio > VST Plug-in Manager > Rescan All** to test this release.
