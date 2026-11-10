# Local builds

The Windows x64 app, VST3, tests, and Cubase ZIP can be built without a GitHub Actions runner:

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File .\Build-Windows.ps1
```

Once Visual Studio's ARM64 C++ tools are installed, build the Windows ARM64 package from the same Windows x64 computer with:

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File .\Build-Windows.ps1 -Architecture ARM64
```

The script configures a dedicated `build/local-windows-x64` directory by default, builds the app and VST3, runs CTest, assembles the standalone app, plugin bundle and installer, and writes the ZIP to `latest-publish/MagDrum-Windows-x64.zip`. With `-Architecture ARM64` it uses a separate build directory and writes `MagDrum-Windows-ARM64.zip`. If the default CMake build folder is locked or damaged, pass a fresh repository-relative path, for example `-BuildDirectory build/local-windows-x64-clean`. The script prints the SHA-256 for release notes. It does not create a GitHub release. After reviewing the packages, use `Publish-LocalRelease.ps1` with an explicit test tag and asset list; it uploads directly to GitHub Releases without starting Actions.

The first build compiles JUCE and takes several minutes. Later runs reuse the CMake build. Requirements are CMake, Git, Visual Studio's C++ desktop workload, and a Windows x64 host. ARM64 cross-compilation additionally requires Visual Studio's ARM64 C++ tools. `-SkipTests` omits the test executables and CTest run.

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

On Ubuntu, run `bash tools/Install-LinuxBuildDependencies.sh` to install the compiler, CMake, Git, and JUCE's ALSA, JACK, LADSPA, FreeType, Fontconfig, X11, EGL and Mesa GLU development packages. The WebKitGTK and curl packages are not needed because this project's JUCE targets compile with `JUCE_WEB_BROWSER=0` and `JUCE_USE_CURL=0`. Then build with `bash ./Build-Unix.sh`. For faster builds, clone the repo inside the WSL Linux filesystem (for example `~/src/MagDrum`) rather than building under `/mnt/c`; the script writes its package to the clone's `latest-publish` directory. Copy `MagDrum-Linux-x64.tar.gz` from that directory into the Windows checkout's `latest-publish` folder before running `Publish-LocalRelease.ps1`. A WSL archive still needs a smoke test on a supported native Linux host before it is treated as fully verified.

## Building the five desktop targets without hosted runners

The five configured matrix targets are Windows x64, Windows ARM64, macOS Intel, macOS Apple Silicon, and Linux x64. Build and test on the target OS where possible. Windows ARM64 may be cross-compiled from Windows x64 after installing Visual Studio's ARM64 C++ tools. macOS packages require macOS and Xcode; neither macOS architecture can be built on this Windows computer. A single Windows computer therefore cannot produce and verify all five platform packages.

The Windows x64 app and VST3 built locally with Visual Studio 2026. Both CTest targets passed (2/2), including 250,000-row paginated browsing with a checked name-ordering index, sample-scan restart recovery, plugin-worker crash recovery, realtime pacing, capture sample-rate metadata, a virtual callback-driven render, VST3 velocity-layer playback/state restoration, a headless end-to-end load of the built VST3 through its isolated scanner with an audible note-36 capture, exact requested-velocity handling, filename-boundary and SFZ family classifier checks, classifier-tag persistence/search and existing-library reclassification with manual metadata preservation, articulation-tag classification, persistence and search, generated WAV spectral-flatness persistence, noise-based hi-hat classification, scanned pitched-tom classification, a four-pulse transient-count/Clap classification and SQLite persistence fixture, plus 24 varied MIDI-file round trips covering 1,536 events. The v0.1.1-test.72 Windows x64 archive was created after both CTest suites passed. Its installer was extracted and run in `-WhatIf` mode; it detected the existing system-wide MagDrum VST3 and described the per-user destination and administrator-only replacement option. The app and VST3 Windows file metadata report version 0.1.1. The local x64 archive is `latest-publish/MagDrum-Windows-x64.zip`, SHA-256 `7e5b0dd6b9642c191695793720179abf7091c1fa5cf067d39278abfba677b146`. It is published as [v0.1.1-test.72](https://github.com/MagnarHarestad/MagDrum/releases/tag/v0.1.1-test.72); both uploaded ZIPs were downloaded and their hashes matched the local archives. The Windows mobile preview target also compiled, and both of its CTest suites passed (2/2); it verifies shared C++ only, not iOS document-provider behavior or AUv3 signing/device operation.

### Windows ARM64 and remaining local hosts

The Visual Studio 2026 Professional installation includes `Microsoft.VisualStudio.Component.VC.Tools.ARM64`; `vswhere` detects it and the local script produced the ARM64 app, VST3, and ZIP. Both binaries report ARM64 architecture. A fresh local rebuild on 2026-11-09 produced `latest-publish/MagDrum-Windows-ARM64.zip` with SHA-256 `ba6f9c068a5ee1e6992d6b85ab881b15261f3844b72747656ff2120662a38ca5`; this refreshed archive has not been uploaded to a new release. JUCE disables `moduleinfo.json` generation because its ARM64 helper cannot run on this AMD64 host. Steinberg documents this metadata file as optional; a host can query the module factory instead, potentially taking longer to scan. The ARM64 app and plugin have not been runtime-tested on ARM64 Windows.

WSL 2's Virtual Machine Platform, WSL, and Ubuntu have now been installed locally. Windows reports that the changes take effect after a restart; the computer has not yet been restarted, and Ubuntu's first-run Linux account has not been created. After the next restart, launch Ubuntu from the Start menu and create its Linux username/password. Then follow the Ubuntu dependency steps above and run `bash ./Build-Unix.sh` from a clone in the Linux filesystem. Confirm `wsl --list --verbose` shows Ubuntu on version 2 and confirm `latest-publish/MagDrum-Linux-x64.tar.gz` exists before including it in a release. This WSL archive still needs a smoke test on a supported native Linux host before it is treated as fully verified.

The `MagDrumMobilePreview` Windows target also compiled successfully from the shared mobile UI sources. CTest passed both existing suites (2/2), including file-URL import/export copying, cancellation, self-copy protection, and paged database search/filter behavior. This verifies shared C++ code only; iOS document-provider access, AUv3 signing, app-group sharing, and device audio still need an Apple build and device validation.

GitHub Actions attempts for all five configured platforms were blocked by the same billing limit before any job steps began. The local build scripts do not consume Actions minutes. On this Windows computer, x64 is runtime-tested and ARM64 compiles and packages locally but lacks native runtime testing; WSL and Ubuntu are installed but await a Windows restart and initial account setup; and both macOS targets need a Mac with Xcode. Thus local builds can replace the hosted process, but this computer alone cannot produce and verify all five platform packages.

Build each package on its required host, then transfer only the finished archives to the Windows release machine and put them in `latest-publish` using the names listed by `Publish-LocalRelease.ps1`. For example, a Mac contributes its Intel/Apple Silicon ZIPs and a Linux host contributes its x64 tarball. Verify each machine's printed SHA-256 after transfer. The publisher calculates checksums again, attaches a checksum file, lists missing platforms, and uploads only the explicitly selected archives through GitHub's Releases API. This upload does not start Actions. The repository workflow is `workflow_dispatch` only, so commits and release tags do not launch its matrix automatically. If GitHub CLI authentication is expired, authenticate once with `gh auth login -h github.com`, then rerun the local publish command:

```powershell
$assets = @('MagDrum-Windows-x64.zip') # Add only packages built and checked on their target hosts.
powershell -NoProfile -ExecutionPolicy Bypass -File .\Publish-LocalRelease.ps1 `
  -Tag v0.1.1-test.N -Asset $assets
```

The package arguments are explicit by design. A release may contain only the platforms available for that test; its generated notes identify the missing packages. Windows x64 is runtime-tested. Windows ARM64 is included as an architecture-checked test package but has not been runtime-tested on ARM64 Windows. macOS Intel, macOS Apple Silicon, and Linux x64 packages are not present.

## Other platform builds

- **Windows ARM64:** The app and VST3 have been cross-compiled from this x64 host, and both binaries in `latest-publish/MagDrum-Windows-ARM64.zip` report ARM64 architecture. The ZIP is attached to v0.1.1-test.72. JUCE omitted the optional `moduleinfo.json` because its ARM64 helper cannot run on an AMD64 host; native ARM64 Windows runtime testing remains outstanding.
- **Linux x64:** use `Build-Unix.sh` natively or through the installed Ubuntu WSL2 distribution after Windows is restarted to finish enabling WSL, an Ubuntu account is created, and JUCE dependencies are installed. The resulting archive still needs a native Linux smoke test.
- **macOS Intel and Apple Silicon, AU:** use `Build-Unix.sh` on a Mac with compatible Xcode and the required SDKs. Those Apple targets cannot be built on the current Windows host.
- **iOS/iPadOS AUv3:** an initial source target is present, and its shared C++ builds as a Windows desktop preview. The iOS target itself has not been built or packaged. It requires macOS/Xcode, a registered Apple app group, signing setup, and device testing.

**Local flow:** build and test a package on its available host using `Build-Windows.ps1` or `Build-Unix.sh`, transfer finished archives to the release machine, and optionally run `Publish-LocalRelease.ps1` with explicit archive names. To test Windows x64 in Cubase, use `latest-publish/MagDrum-Windows-x64.zip` and follow the bundled installer README; uploading a release is not required for local testing. No Actions minutes are used. Windows ARM64 can be cross-compiled here, but requires native runtime testing. Linux is blocked until Windows is restarted to activate the WSL components and Ubuntu first-run setup completes; Intel and Apple Silicon macOS still need a Mac (Apple Silicon can cross-build Intel). This computer alone cannot produce and verify all five desktop packages. Self-hosted Actions runners are not required. `Publish-LocalRelease.ps1` authenticated through the Git Credential Manager keyring and created releases without starting GitHub Actions jobs.
