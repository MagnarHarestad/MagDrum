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

The Windows x64 app and VST3 built successfully locally, both CTest targets passed (2/2), and `Build-Windows.ps1` created `latest-publish/MagDrum-Windows-x64.zip`. The current local archive has SHA-256 `6a8ca4a2efdcdecbd572cc48a901d4ee5dfb162cea1d9f3692ff3f7e19499f99`. It includes MIDI import reporting for unmapped and ambiguous note assignments, an imported-versus-current MIDI pattern diff, conservative feature-only kick classification with synthetic high-pitch and low-transient checks, automatic VST3 preset searches in standard platform locations plus a custom-folder option, plugin note scanning in a separate process with shared cancellation/progress supervision, source-kit recall for plugin previews, classification-confidence review, confirmed velocity-range and round-robin take-group recording, looped sequencer preview, tempo and time-signature editing at arbitrary beats, and the PowerShell VST3 installer. Tests cover preset path priority/deduplication, processor WAV loading and state restore, MIDI playback, MIDI diffing, classifier false-positive checks, ambiguous MIDI mappings, confirmed-range validation, round-robin membership and invalidation on rescan, worker abnormal-exit isolation and velocity response. The fixture simulates a child-process abort; a real third-party plugin crash fixture remains outstanding. The archive can be uploaded through the GitHub Releases API without starting Actions.

GitHub Actions attempts for all five configured platforms were blocked by the same billing limit before any job steps began. The local build scripts do not consume Actions minutes. On this Windows computer, x64 is available and verified; ARM64 preflight fails because the Visual Studio ARM64 C++ component is missing; Linux x64 needs a usable Linux host or WSL distribution (none is currently available); and both macOS targets need a Mac with Xcode. Thus local builds can replace the hosted process, but this computer alone cannot produce all five platform packages.

When archives are available from each host, collect them in `latest-publish`. Publish only the archives you have just built and checked, naming them explicitly so a stale archive cannot slip into a release. `Publish-LocalRelease.ps1` calculates SHA-256 values, attaches a checksum file, lists platforms that are missing, and uploads the selected packages through GitHub's Releases API. It does not start Actions. The repository workflow is `workflow_dispatch` only, so neither local commits nor release tags launch its build matrix:

```powershell
$assets = @('MagDrum-Windows-x64.zip') # Add only packages built and checked on their target hosts.
powershell -NoProfile -ExecutionPolicy Bypass -File .\Publish-LocalRelease.ps1 `
  -Tag v0.1.1-test.N -Asset $assets
```

The package arguments are explicit by design. A release may contain only the platforms available for that test; its generated notes identify the missing packages. Windows x64 is currently available. Windows ARM64, macOS Intel, macOS Apple Silicon, and Linux x64 packages are not present in `latest-publish`.

## Other platform builds

- **Windows ARM64:** Visual Studio can cross-compile from x64 once its ARM64 C++ toolset is installed. The current machine's Visual Studio installation does not contain that toolset; the local script confirmed the missing component before compilation.
- **Linux x64:** use `Build-Unix.sh` natively or through a configured WSL2 distribution with the JUCE Linux development libraries installed. No usable WSL distribution is currently available on this Windows machine.
- **macOS Intel and Apple Silicon, AU:** use `Build-Unix.sh` on a Mac with compatible Xcode and the required SDKs. Those Apple targets cannot be built on the current Windows host.
- **iOS/iPadOS AUv3:** requires a separate Apple mobile target, AUv3 implementation, signing setup, and device testing; the current project does not contain that target.

**Local release flow:** build and test a package on its available host using `Build-Windows.ps1` or `Build-Unix.sh`, then use `Publish-LocalRelease.ps1` with the explicit archive name(s). GitHub Releases remains the download location, but build, test and packaging run locally, outside Actions billing. One Windows x64 computer cannot produce all five packages: ARM64 needs its Visual Studio cross-toolchain; Linux needs an available Linux/WSL host; Intel and Apple Silicon macOS need a Mac (Apple Silicon can cross-build Intel). Self-hosted Actions runners are unnecessary for this manual process.
