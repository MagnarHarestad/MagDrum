# Latest MagDrum packages

Download the current Windows x64 test package from [MagDrum v0.1.1-test.17](https://github.com/MagnarHarestad/MagDrum/releases/tag/v0.1.1-test.17).

The current package is:

- `MagDrum-Windows-x64.zip` — standalone app, VST3 and PowerShell installer.

The package is intended for testing the VST3 instrument in Cubase Pro 15. Its installer defaults to the per-user VST3 location and preserves an existing MagDrum installation when replacing it. The standalone scanner lists programs exposed by drum instruments and lets users select them before saving kit state or scanning notes. MIDI export and import preserve pattern length, including empty bars, and the step sequencer can render and play a sample-based audio preview with tempo changes. Sample analysis now averages spectral centroid across four positions in the file; older indexed files are reanalyzed once. SHA-256: `73ff83385845f02ada26ae762a4297fe9645a6d7ad37adc18371ea735e675fd7`.

GitHub Actions is manual-only while hosted billing is blocked. This package was built and tested locally on Windows x64; CTest passed (1/1). Windows ARM64 can be cross-compiled from Windows x64 after installing Visual Studio's ARM64 C++ tools. macOS Intel/Apple Silicon and AU packages must be built on a Mac with Xcode; Linux x64 can be built on Linux or in WSL2. Local build scripts, tested package details, and host requirements are documented in [local-builds.md](../docs/local-builds.md). The other four platform archives have not been produced yet.

Archives generated locally are ignored here; this directory is the in-repository package index.
