# Latest MagDrum packages

Download the current Windows x64 test package from [MagDrum v0.1.1-test.15](https://github.com/MagnarHarestad/MagDrum/releases/tag/v0.1.1-test.15).

The current package is:

- `MagDrum-Windows-x64.zip` — standalone app, VST3 and PowerShell installer.

The package is intended for testing the VST3 instrument in Cubase Pro 15. Its installer defaults to the per-user VST3 location and preserves an existing MagDrum installation when replacing it. The standalone scanner lists programs exposed by drum instruments and lets users select them before saving kit state or scanning notes. MIDI export and import also preserve pattern length, including empty bars. Its SHA-256 is `c5924478ab9c65799bc509cbd94d2a9b92301cab4fed126217feb5e7f5e42307`.

GitHub Actions is manual-only while hosted billing is blocked, so platform builds are being prepared to run locally. Windows ARM64 can be cross-compiled from Windows x64 after installing Visual Studio's ARM64 C++ tools. macOS Intel/Apple Silicon and AU packages must be built on a Mac with Xcode; Linux x64 can be built on Linux or in WSL2. Local build scripts and host requirements are documented in [local-builds.md](../docs/local-builds.md). Those additional archives have not been produced yet.

Archives generated locally are ignored here; this directory is the in-repository package index.
