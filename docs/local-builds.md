# Local builds

The Windows x64 app, VST3, tests, and Cubase ZIP can be built without a GitHub Actions runner:

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File .\Build-Windows.ps1
```

The script configures a dedicated `build/local-windows-x64` directory, builds the app and VST3, runs CTest, assembles the standalone app, plugin bundle and installer, and writes the ZIP to `latest-publish/MagDrum-Windows-x64.zip`. It prints the SHA-256 for release notes. It does not create a GitHub release; use `gh release create` after reviewing the package and selecting a release tag.

The first build compiles JUCE and takes several minutes. Later runs reuse the CMake build. Requirements are CMake, Git, Visual Studio's C++ desktop workload, and a Windows x64 host. The script currently packages only Windows x64.

## Other platform builds

- **Windows ARM64:** Visual Studio can cross-compile from x64 once its ARM64 C++ toolset is installed. The current machine's Visual Studio installation does not contain that toolset yet.
- **Linux x64:** build natively on Linux or through a configured WSL2 distribution with the JUCE Linux development libraries installed. A usable WSL distribution is not currently available on this Windows machine.
- **macOS Intel and Apple Silicon, AU:** build on a Mac with compatible Xcode and the required SDKs. Those Apple targets cannot be built on the current Windows host.
- **iOS/iPadOS AUv3:** requires a separate Apple mobile target, AUv3 implementation, signing setup, and device testing; the current project does not contain that target.

GitHub Actions remains configured as a remote matrix for when hosted billing is available. GitHub documents self-hosted runner compute as free, but those are still GitHub Actions jobs and require a separate runner on each operating system that must build natively. See [GitHub Actions billing](https://docs.github.com/en/billing/concepts/product-billing/github-actions) and [self-hosted runners](https://docs.github.com/en/actions/how-tos/manage-runners/self-hosted-runners/add-runners).
