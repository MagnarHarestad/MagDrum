# MagDrum local test packages

Public release [v0.1.1-test.140](https://github.com/MagnarHarestad/MagDrum/releases/tag/v0.1.1-test.140) contains Windows x64, Windows ARM64 and Linux x64. The Windows UI avoids non-ASCII punctuation in the sample-sort label and scan summary to prevent code-page glyph corruption; all eight language choices remain available. The Linux archive includes a per-user installer for the app and VST3; its update test confirms old bundles are backed up outside plug-in scan paths. Windows x64 and Linux Release CTest suites passed (2/2 each). Windows ARM64 app, VST3 and helper were cross-compiled; all three PE headers identify ARM64. ARM64 runtime testing remains outstanding. Linux CTest loaded the byte-identical release module through JUCE's VST3 host, scanned MIDI notes 0–127 and verified audible note 36 after restoring saved state. Ardour 8.4 scanner also recognized the package in Ubuntu 24.04 WSL2. An interactive Ardour session and physical Linux audio-device test remain outstanding.

- Windows x64 SHA-256: `322b66b8847b50760eebd29ade387d9e2b946f811041cc07e45d103f73e54356`
- Windows ARM64 SHA-256: `38b4c7e65b3fb5018b036e3fe4a7c51e9e477dadadb80dda251d247801bba2d9`
- Linux x64 SHA-256: `8cd77994101d08dbaccedebff3b606e3bea0409adfc5595acf3e37d614ebba3a`

The system-installed Windows VST3 remains .135. The installer refuses replacement while Cubase15.exe is running, including when it has no visible window. After that process exits, run the bundled `Install-MagDrum.ps1 -AllUsers` script as administrator. It installs into `C:\Program Files\Common Files\VST3\MagDrum.vst3` and moves the previous bundle to `%ProgramData%\MagDrum\VST3-backups`. Restart Cubase and rescan its VST3 plug-ins to use the new build.
