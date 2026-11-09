# Local builds

The Windows x64 app, VST3, tests, and Cubase ZIP can be built without a GitHub Actions runner:

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File .\Build-Windows.ps1
```

Once Visual Studio's ARM64 C++ tools are installed, build the Windows ARM64 package from the same Windows x64 computer with:

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File .\Build-Windows.ps1 -Architecture ARM64
```

The script configures a dedicated `build/local-windows-x64` directory by default, builds the app and VST3, runs CTest, assembles the standalone app, plugin bundle and installer, and writes the ZIP to `latest-publish/MagDrum-Windows-x64.zip`. With `-Architecture ARM64` it uses a separate build directory and writes `MagDrum-Windows-ARM64.zip`. If the default CMake build folder is locked or damaged, pass a fresh repository-relative path, for example `-BuildDirectory build/local-windows-x64-clean`. The script prints the SHA-256 for release notes. It does not create a GitHub release; use `gh release create` after reviewing the package and selecting a release tag.

The first build compiles JUCE and takes several minutes. Later runs reuse the CMake build. Requirements are CMake, Git, Visual Studio's C++ desktop workload, and a Windows x64 host. ARM64 packaging additionally requires Visual Studio's ARM64 C++ tools.

On a native Linux or macOS host, use the matching local script:

```sh
bash ./Build-Unix.sh
```

On macOS this builds the host architecture by default. An Apple Silicon Mac with Xcode can also configure an Intel build using `bash ./Build-Unix.sh x86_64`; use `bash ./Build-Unix.sh arm64` for an Apple Silicon build. Each archive is written to `latest-publish`. macOS packages include the app, VST3, and AU bundles. Linux packages include the app and VST3. The script runs CTest before packaging and prints the archive SHA-256.

Linux needs CMake, Git, a C++ compiler, and JUCE's Linux development dependencies: ALSA, JACK, FreeType, Fontconfig, X11 (including XComposite, Xcursor, Xext, Xinerama, Xrandr, and Xrender), WebKitGTK 4.1, and Mesa GLU development packages. Install these with the distribution package manager before running the script. WSL2 can provide the Linux x64 build after a Linux distribution and the same packages are installed; the archive should still be tested on a supported native Linux host.

## Building the five desktop targets without hosted runners

The five configured matrix targets are Windows x64, Windows ARM64, macOS Intel, macOS Apple Silicon, and Linux x64. Build and test on the target OS where possible. Windows ARM64 may be cross-compiled from Windows x64 after installing Visual Studio's ARM64 C++ tools. macOS packages require macOS and Xcode; neither macOS architecture can be built on this Windows computer. A single Windows computer therefore cannot produce and verify all five platform packages.

The local Windows x64 app and VST3 built successfully, CTest passed (1/1), and `Build-Windows.ps1` created `latest-publish/MagDrum-Windows-x64.zip`. For v0.1.1-test.21, its SHA-256 is `5e80420293a5d250b0620a1e3989fee5f524d5b36ec5f09e9e89f96a99872627`. GitHub CLI uploaded this package to Releases; no Actions run was started for the release.

When archives are available from each host, collect them in `latest-publish` and publish them from a local machine with GitHub CLI. GitHub release creation uses the GitHub API; it does not start a hosted build. The workflow is manual-only, so local commits and release tags do not launch the hosted build matrix:

```powershell
$tag = 'v0.1.1-test.N' # Replace N with the next unused test number.
gh release create $tag `
  .\latest-publish\MagDrum-Windows-x64.zip `
  .\latest-publish\MagDrum-Windows-ARM64.zip `
  .\latest-publish\MagDrum-macOS-Intel.zip `
  .\latest-publish\MagDrum-macOS-AppleSilicon.zip `
  .\latest-publish\MagDrum-Linux-x64.tar.gz `
  --prerelease --generate-notes
```

Only include archives that exist and have passed their host-side checks. A release may contain only the platforms available for that test; its notes must identify the missing packages. The v0.1.1-test.21 release contains Windows x64. Windows ARM64, macOS Intel, macOS Apple Silicon, and Linux x64 packages are not present in `latest-publish`.

## Other platform builds

- **Windows ARM64:** Visual Studio can cross-compile from x64 once its ARM64 C++ toolset is installed. The current machine's Visual Studio installation does not contain that toolset yet.
- **Linux x64:** use `Build-Unix.sh` natively or through a configured WSL2 distribution with the JUCE Linux development libraries installed. No usable WSL distribution is currently available on this Windows machine.
- **macOS Intel and Apple Silicon, AU:** use `Build-Unix.sh` on a Mac with compatible Xcode and the required SDKs. Those Apple targets cannot be built on the current Windows host.
- **iOS/iPadOS AUv3:** requires a separate Apple mobile target, AUv3 implementation, signing setup, and device testing; the current project does not contain that target.

**Recommended local release flow:** build and test each package on its native host with `Build-Windows.ps1` or `Build-Unix.sh`, copy the resulting archives into `latest-publish`, verify each SHA-256, then create a release from a machine with `gh` authenticated. This does not dispatch the repository's workflow: `.github/workflows/build.yml` only runs for `workflow_dispatch`, and `gh release create` uploads the packages through GitHub's release API rather than starting build runners. This avoids the current Actions billing block while keeping GitHub Releases as the download location. Native Windows, macOS and Linux machines are still needed to produce and validate all five targets; Windows ARM64 can be cross-compiled from Windows x64 with the extra toolset. Self-hosted Actions runners are a separate option, but remain tied to Actions orchestration and are not needed for this manual process.
