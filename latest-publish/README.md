# Latest MagDrum packages

The current Windows x64 app and VST3 build is in `MagDrum-Windows-x64.zip` and GitHub release [v0.1.1-test.102](https://github.com/MagnarHarestad/MagDrum/releases/tag/v0.1.1-test.102). SHA-256: `f38ea8ce15df9d1cf320fa49f4f59fd13baafa69dd1a1aa147d1245e42fb09e4`.

The Windows ARM64 cross-compiled test package is `MagDrum-Windows-ARM64.zip`, release [v0.1.1-test.98](https://github.com/MagnarHarestad/MagDrum/releases/tag/v0.1.1-test.98). SHA-256: `84ed40de2e546f778409a60eccd3c2e10eb8438d54d653c16c6375381b18ab3d`. The app, VST3 and both locally built test executables report ARM64 architecture. The app and tests have not been run on ARM64 Windows.

Release packages include the standalone app, VST3 bundle, installer and third-party license notices. In .102 the x64 app and VST3 targets compiled with Visual Studio 2026; tests were not run for this language update. The burger-menu language setting refreshes the VST views, filters, step tooltips, mapping names and sample-layer summaries and persists across launches. English is the initial default; German, French, Spanish, Swedish, Norwegian, Danish and Finnish are available. This package retains the .100 source-kit flow and .99 machined controls. The ARM64 app, VST3 and test executables in .98 compiled and linked, but neither tests nor runtime were run because the build host is x64.

Generated archives are ignored in this directory; this file is the checked-in package index.
