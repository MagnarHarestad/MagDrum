# Latest MagDrum packages

Download the current Windows x64 test package from [MagDrum v0.1.1-test.16](https://github.com/MagnarHarestad/MagDrum/releases/tag/v0.1.1-test.16).

The current package is:

- `MagDrum-Windows-x64.zip` — standalone app, VST3 and PowerShell installer.

The package is intended for testing the VST3 instrument in Cubase Pro 15. Its installer defaults to the per-user VST3 location and preserves an existing MagDrum installation when replacing it. The standalone scanner lists programs exposed by drum instruments and lets users select them before saving kit state or scanning notes. MIDI export and import preserve pattern length, including empty bars, and the step sequencer can render and play a sample-based audio preview with tempo changes. SHA-256: `a0d2f3d5fac457ab7af74f03f8dd51d2152961181208034fd8c62efd2440e39b`.

GitHub Actions is manual-only while hosted billing is blocked. This package was built and tested locally on Windows x64; CTest passed (1/1). Windows ARM64 can be cross-compiled from Windows x64 after installing Visual Studio's ARM64 C++ tools. macOS Intel/Apple Silicon and AU packages must be built on a Mac with Xcode; Linux x64 can be built on Linux or in WSL2. Local build scripts, tested package details, and host requirements are documented in [local-builds.md](../docs/local-builds.md). The other four platform archives have not been produced yet.

Archives generated locally are ignored here; this directory is the in-repository package index.
