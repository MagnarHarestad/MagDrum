# MagDrum local test packages

Public release [v0.1.1-test.139](https://github.com/MagnarHarestad/MagDrum/releases/tag/v0.1.1-test.139) contains Windows x64, Windows ARM64 and Linux x64. The Linux archive now includes a per-user installer for the app and VST3; its update test confirms old bundles are backed up outside plug-in scan paths. Windows x64 and Linux Release CTest suites passed (2/2 each). Windows ARM64 app, VST3 and helper were cross-compiled; all three PE headers identify ARM64. ARM64 runtime testing remains outstanding. Linux CTest loaded the byte-identical release module through JUCE's VST3 host, scanned MIDI notes 0–127 and verified audible note 36 after restoring saved state. Ardour 8.4 scanner also recognized the package in Ubuntu 24.04 WSL2. An interactive Ardour session and physical Linux audio-device test remain outstanding.

- Windows x64 SHA-256: `eb374ff329d6908f2b7101a3e920e44a6c3ffa1b636567bd1d9ca66c052d9fe3`
- Windows ARM64 SHA-256: `b8962f6e3b1cfba7dc9c3d03871d0d919ba1374308ad4a9d171e21c6351f7b71`
- Linux x64 SHA-256: `2584aedbad7493e4f5fae76ff271754542677176e0701e5acfac413b12e0f292`

The system-installed Windows VST3 remains .135. The .138 installer refuses replacement while Cubase15.exe is running, including when it has no visible window. After that process exits, run the bundled `Install-MagDrum.ps1 -AllUsers` script as administrator. It installs into `C:\Program Files\Common Files\VST3\MagDrum.vst3` and moves the previous bundle to `%ProgramData%\MagDrum\VST3-backups`. Restart Cubase and rescan its VST3 plug-ins to use the new build.
