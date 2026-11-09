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

The iOS library UI's shared C++ can be compile-checked on Windows with a desktop preview target. This does not produce an iOS app or verify Apple entitlements:

```powershell
cmake -S . -B build/local-windows-x64-mobile-preview -A x64 `
  -DBUILD_TESTING=ON -DMAGDRUM_BUILD_MOBILE_PREVIEW=ON
cmake --build build/local-windows-x64-mobile-preview --config Release --target MagDrumMobilePreview
ctest --test-dir build/local-windows-x64-mobile-preview -C Release --output-on-failure
```

On a native Linux or macOS host, use the matching local script:

```sh
bash ./Build-Unix.sh
```

On macOS this builds the host architecture by default. An Apple Silicon Mac with Xcode can also configure an Intel build using `bash ./Build-Unix.sh x86_64`; use `bash ./Build-Unix.sh arm64` for an Apple Silicon build. Each archive is written to `latest-publish`. macOS packages include the app, VST3, and AU bundles. Linux packages include the app and VST3. The script runs CTest before packaging and prints the archive SHA-256.

Linux needs CMake, Git, a C++ compiler, and JUCE's Linux development dependencies: ALSA, JACK, FreeType, Fontconfig, X11 (including XComposite, Xcursor, Xext, Xinerama, Xrandr, and Xrender), WebKitGTK 4.1, and Mesa GLU development packages. Install these with the distribution package manager before running the script. WSL2 can provide the Linux x64 build after a Linux distribution and the same packages are installed; the archive should still be tested on a supported native Linux host.

## Building the five desktop targets without hosted runners

The five configured matrix targets are Windows x64, Windows ARM64, macOS Intel, macOS Apple Silicon, and Linux x64. Build and test on the target OS where possible. Windows ARM64 may be cross-compiled from Windows x64 after installing Visual Studio's ARM64 C++ tools. macOS packages require macOS and Xcode; neither macOS architecture can be built on this Windows computer. A single Windows computer therefore cannot produce and verify all five platform packages.

The Windows x64 app and VST3 built locally with Visual Studio 2026. Both CTest targets passed (2/2), including 250,000-row paginated browsing with a checked name-ordering index, sample-scan restart recovery, plugin-worker crash recovery, realtime pacing, capture sample-rate metadata, a virtual callback-driven render, VST3 velocity-layer playback/state restoration, a headless end-to-end load of the built VST3 through its isolated scanner with an audible note-36 capture, exact requested-velocity handling, filename-boundary classifier checks, classifier-tag persistence/search and existing-library reclassification with manual metadata preservation, articulation-tag classification, persistence and search, generated WAV spectral-flatness persistence, noise-based hi-hat classification and scanned pitched-tom classification, plus 24 varied MIDI-file round trips covering 1,536 events. The packaged installer was extracted and run in `-WhatIf` mode; it detected this machine's existing system-wide MagDrum VST3 and explained how to replace it with a backup. The app and VST3 Windows file metadata report version 0.1.1. The local archive is `latest-publish/MagDrum-Windows-x64.zip`, SHA-256 `d8bcc420474650a375dc50d4169e08c03dae32f6a721d3f7b429c005ffd2a4e3`. It is published locally as [v0.1.1-test.65](https://github.com/MagnarHarestad/MagDrum/releases/tag/v0.1.1-test.65). The Windows mobile preview target also compiled, and both of its CTest suites passed (2/2); it verifies shared C++ only, not iOS document-provider behavior or AUv3 signing/device operation.

The `MagDrumMobilePreview` Windows target also compiled successfully from the shared mobile UI sources. CTest passed both existing suites (2/2), including file-URL import/export copying, cancellation, self-copy protection, and paged database search/filter behavior. This verifies shared C++ code only; iOS document-provider access, AUv3 signing, app-group sharing, and device audio still need an Apple build and device validation.

GitHub Actions attempts for all five configured platforms were blocked by the same billing limit before any job steps began. The local build scripts do not consume Actions minutes. On this Windows computer, x64 is available and verified; ARM64 preflight fails because the Visual Studio ARM64 C++ component is missing; WSL has no installed Linux distribution; and both macOS targets need a Mac with Xcode. Thus local builds can replace the hosted process, but this computer alone cannot produce all five platform packages.

Build each package on its required host, then transfer only the finished archives to the Windows release machine and put them in `latest-publish` using the names listed by `Publish-LocalRelease.ps1`. For example, a Mac contributes its Intel/Apple Silicon ZIPs and a Linux host contributes its x64 tarball. Verify each machine's printed SHA-256 after transfer. The publisher calculates checksums again, attaches a checksum file, lists missing platforms, and uploads only the explicitly selected archives through GitHub's Releases API. This upload does not start Actions. The repository workflow is `workflow_dispatch` only, so commits and release tags do not launch its matrix automatically. If GitHub CLI authentication is expired, authenticate once with `gh auth login -h github.com`, then rerun the local publish command:

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
- **iOS/iPadOS AUv3:** an initial source target is present, and its shared C++ builds as a Windows desktop preview. The iOS target itself has not been built or packaged. It requires macOS/Xcode, a registered Apple app group, signing setup, and device testing.

**Local flow:** build and test a package on its available host using `Build-Windows.ps1` or `Build-Unix.sh`, transfer finished archives to the release machine, and optionally run `Publish-LocalRelease.ps1` with explicit archive names. To test Windows x64 in Cubase, use `latest-publish/MagDrum-Windows-x64.zip` and follow the bundled installer README; uploading a release is not required for local testing. No Actions minutes are used. One Windows x64 computer cannot produce all five desktop packages: ARM64 needs its Visual Studio cross-toolchain; Linux needs a configured Linux/WSL host; Intel and Apple Silicon macOS need a Mac (Apple Silicon can cross-build Intel). This computer currently has neither the ARM64 C++ toolset nor a WSL distribution, so Windows x64 is the only target available here. The local solution is a per-host build-and-transfer flow, not one Windows command that creates all five packages. Self-hosted Actions runners are not required. The publisher successfully used the Windows Git Credential Manager keyring to create v0.1.1-test.53 even though the standalone `gh auth status` check reported a stale CLI token.
