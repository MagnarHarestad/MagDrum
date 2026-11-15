# MagDrum local test packages

Public release [v0.1.1-test.137](https://github.com/MagnarHarestad/MagDrum/releases/tag/v0.1.1-test.137) contains Windows x64, Windows ARM64 and Linux x64. Windows x64 and Linux Release CTest suites passed (2/2 each). Windows ARM64 app, VST3 and helper were cross-compiled; all three PE headers identify ARM64. ARM64 runtime testing remains outstanding.

- Windows x64 SHA-256: `3066cf488d58624f2081434fb35b6617d1b30149a8885b1f0629b842ae118ffb`
- Windows ARM64 SHA-256: `93d1564abd569da9a669b1ab167def2e1c067006061480002a47ea7b3d9db353`
- Linux x64 SHA-256: `1453eab32eb71c489b5d2590d35fb94281db138ec66fa5908e1d88ebe644153e`

The system-installed Windows VST3 remains .135 because the .137 UAC request was cancelled. To install .137, run the bundled `Install-MagDrum.ps1 -AllUsers` script as administrator. It installs into `C:\Program Files\Common Files\VST3\MagDrum.vst3` and moves the previous bundle to `%ProgramData%\MagDrum\VST3-backups`. Restart Cubase and rescan its VST3 plug-ins to use the new build.
