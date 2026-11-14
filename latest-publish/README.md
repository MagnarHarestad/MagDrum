# Latest MagDrum packages

The current Windows x64 app and VST3 build is in `MagDrum-Windows-x64.zip` and GitHub release [v0.1.1-test.103](https://github.com/MagnarHarestad/MagDrum/releases/tag/v0.1.1-test.103). SHA-256: `d8e0cfdc0747c135049620acf96c04e9f569b800d6cb07e3e97bd32da5ab3f0a`.

The current Windows ARM64 app and VST3 package is `MagDrum-Windows-ARM64.zip`, also in release [v0.1.1-test.103](https://github.com/MagnarHarestad/MagDrum/releases/tag/v0.1.1-test.103). SHA-256: `07628f6ccfccc801e074da064a1a2fb8a5b575d48eb0967d08e7f5f155bf334d`. The app, VST3 and both locally built test executables report ARM64 architecture. Runtime and tests remain unverified because this host is x64.

Release packages include the standalone app, VST3 bundle, installer and third-party license notices. In .103 the x64 app and VST3 compiled with Visual Studio 2026, and both CTest suites passed (2/2). The Windows mobile-preview target also compiles; this is not an Apple SDK or AUv3 build. The ARM64 app, VST3 and both test executables compiled and PE headers confirm ARM64, but runtime and CTest require a native ARM64 host. The burger-menu language setting refreshes the VST views, filters, step tooltips, mapping names and sample-layer summaries and persists across launches. English is the initial default; German, French, Spanish, Swedish, Norwegian, Danish and Finnish are available. This package retains the .100 source-kit flow and .99 machined controls.

Generated archives are ignored in this directory; this file is the checked-in package index.
