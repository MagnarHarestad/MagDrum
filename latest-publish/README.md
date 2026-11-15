# MagDrum local test packages

Release [v0.1.1-test.135](https://github.com/MagnarHarestad/MagDrum/releases/tag/v0.1.1-test.135) contains Windows x64 and Linux x64 builds. The sequencer grid follows MIDI time-signature changes placed on sixteenth-note boundaries, including partial bars, and rejects non-finite event positions before sorting. Both Release CTest suites passed on each platform (2/2).

- Windows x64 SHA-256: `33a536870fa96467863e727e68c57bd67f0938103940013759f3e77eb262633e`
- Linux x64 SHA-256: `7afa579d3dd7fdc888a6cfb5b42fd80ad6115229a3187b7cd87867097ccfaf1a`

MagDrum v0.1.1-test.135 is installed in `C:\\Program Files\\Common Files\\VST3\\MagDrum.vst3`. Version .134 is backed up under `%ProgramData%\\MagDrum\\VST3-backups`, outside Cubase's VST3 scan folder. Windows confirmed that the background Cubase15 process has no MagDrum module loaded and only one MagDrum bundle remains in the system VST3 folder. Restart Cubase and choose **Studio > VST Plug-in Manager > Rescan All** to test this release.
