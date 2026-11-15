# MagDrum local test packages

Release [v0.1.1-test.136](https://github.com/MagnarHarestad/MagDrum/releases/tag/v0.1.1-test.136) contains Windows x64, Windows ARM64, and Linux x64 packages. The sequencer grid follows MIDI time-signature changes placed on sixteenth-note boundaries, including partial bars, and rejects non-finite event positions before sorting. Windows x64 and Linux Release CTest suites passed (2/2 each). Windows ARM64 app, VST3 and helper binaries were cross-compiled; all three PE headers identify ARM64. Runtime testing on ARM64 Windows remains outstanding.

- Windows x64 SHA-256: `33a536870fa96467863e727e68c57bd67f0938103940013759f3e77eb262633e`
- Windows ARM64 SHA-256: `0902e43c1eecf99df9e23edb2b254214fa31482fd87716db1b13353c67b1c2d5`
- Linux x64 SHA-256: `7afa579d3dd7fdc888a6cfb5b42fd80ad6115229a3187b7cd87867097ccfaf1a`

MagDrum v0.1.1-test.135 is installed in `C:\\Program Files\\Common Files\\VST3\\MagDrum.vst3`. The .136 Windows x64 package has identical application/plugin contents; its additional Windows ARM64 package is cross-compiled and is not runtime-verified. Version .134 is backed up under `%ProgramData%\\MagDrum\\VST3-backups`, outside Cubase's VST3 scan folder. Windows confirmed that the background Cubase15 process has no MagDrum module loaded and only one MagDrum bundle remains in the system VST3 folder. Restart Cubase and choose **Studio > VST Plug-in Manager > Rescan All** to test the installed x64 build.
