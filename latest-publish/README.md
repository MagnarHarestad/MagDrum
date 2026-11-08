# Latest MagDrum packages

Download the current Windows x64 test package from [MagDrum v0.1.1-test.13](https://github.com/MagnarHarestad/MagDrum/releases/tag/v0.1.1-test.13).

The current package is:

- `MagDrum-Windows-x64.zip` — standalone app, VST3 and PowerShell installer.

The package is intended for testing the VST3 instrument in Cubase Pro 15. Its installer defaults to the per-user VST3 location and preserves an existing MagDrum installation when replacing it. The standalone browser can sort by name, duration and spectral brightness and shows plugin source/kit and MIDI mapping metadata. Its SHA-256 is `e96c79968d5a800a6d98ab322ca6f8fc62fc6fdaeea50e4d45d6df4753e388f6`.

GitHub Actions is manual-only while hosted billing is blocked, so platform builds are being prepared to run locally. Windows ARM64 can be cross-compiled from Windows x64 after installing Visual Studio's ARM64 C++ tools. macOS Intel/Apple Silicon and AU packages must be built on a Mac with Xcode; Linux x64 can be built on Linux or in WSL2. Local build scripts and host requirements are documented in [local-builds.md](../docs/local-builds.md). Those additional archives have not been produced yet.

Archives generated locally are ignored here; this directory is the in-repository package index.
