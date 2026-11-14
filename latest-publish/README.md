# Latest MagDrum packages

The current Windows x64 app and VST3 test build is in `MagDrum-Windows-x64.zip` and GitHub release [v0.1.1-test.100](https://github.com/MagnarHarestad/MagDrum/releases/tag/v0.1.1-test.100). SHA-256: `0596a3762682b5f0ef486006ae2b1c4a08f51eea8617d7fd4e3a4e4c8dffe8b2`.

The Windows ARM64 cross-compiled test package is `MagDrum-Windows-ARM64.zip`, release [v0.1.1-test.98](https://github.com/MagnarHarestad/MagDrum/releases/tag/v0.1.1-test.98). SHA-256: `84ed40de2e546f778409a60eccd3c2e10eb8438d54d653c16c6375381b18ab3d`. The app, VST3 and both locally built test executables report ARM64 architecture. The app and tests have not been run on ARM64 Windows.

Release packages include the standalone app, VST3 bundle, installer and third-party license notices. In .100 the x64 app and VST3 targets compiled with Visual Studio 2026, and both CTest suites passed. The VST Sample Library can open a captured sample's saved source instrument and kit in the isolated MagDrum helper; request data is validated and its new UI messages are translated into all eight languages. This package retains the .99 machined controls. The ARM64 app, VST3 and test executables in .98 compiled and linked, but neither tests nor runtime were run because the build host is x64. English remains the default.

Generated archives are ignored in this directory; this file is the checked-in package index.
