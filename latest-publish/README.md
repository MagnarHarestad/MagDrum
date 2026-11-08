# Latest MagDrum packages

Download the current Windows x64 test package from [MagDrum v0.1.1-test.12](https://github.com/MagnarHarestad/MagDrum/releases/tag/v0.1.1-test.12).

The current package is:

- `MagDrum-Windows-x64.zip` — standalone app, VST3 and PowerShell installer.

The package is intended for testing the VST3 instrument in Cubase Pro 15. The standalone browser can sort by name, duration and spectral brightness and shows plugin source/kit and MIDI mapping metadata. Its SHA-256 is `53f2666354caf4b6f864aeb43b36e7af122699b04b404f5f47be62c83e439512`.

GitHub Actions is currently blocked by account billing, so platform builds are being prepared to run locally. Windows ARM64 can be cross-compiled from Windows x64 after installing Visual Studio's ARM64 C++ tools. macOS Intel/Apple Silicon and AU packages must be built on a Mac with Xcode; Linux x64 can be built on Linux or in WSL2. Local build scripts and host requirements are documented in [local-builds.md](../docs/local-builds.md). Those additional archives have not been produced yet.

Archives generated locally are ignored here; this directory is the in-repository package index.
