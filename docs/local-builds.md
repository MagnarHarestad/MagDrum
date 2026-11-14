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

Release v0.1.1-test.115 contains Windows x64 (SHA-256: `622e22fb220abec3365985efb0e9a58b219a4e5b8a9afe16a075b2d05d2a13e2`). Its app and VST3 built locally, and both Release CTest suites passed (2/2). Debug app, VST3 and test targets also build, and both Debug CTest suites pass (2/2). Release v0.1.1-test.113 contains Windows ARM64 (SHA-256: `97ced5a16d145c0cb2b059b2b0275c0ca3f9de906c1317123ea7c290aa53b326`); app and VST3 binaries were cross-compiled, but were not run on ARM64 hardware. Release v0.1.1-test.111 contains Linux x64 (SHA-256: `5d44967656b88dd2dd498d4830b33f8ffc0076fb5a1cd6082b73aa06c1604caf`). Linux plugin tests passed, while CTest reported the app integration test failed without output even though direct invocation passed. The Ubuntu WSL distribution is currently unavailable in this Windows environment, and native Linux host smoke testing remains pending. JUCE omits the optional ARM64 `moduleinfo.json`. macOS and iOS/AUv3 remain unverified. Earlier x64 package v0.1.1-test.112 uses SHA-256 `773c3f71569c9319f7727895064a06df3ab64000abce4f437b533d32de29c8d6`.

### Windows ARM64 and remaining local hosts

The Visual Studio 2026 Professional installation includes `Microsoft.VisualStudio.Component.VC.Tools.ARM64`; `vswhere` detects it. Release v0.1.1-test.113 contains the ARM64 app and VST3 package, cross-compiled on x64. Both test executables were also cross-compiled locally. `dumpbin /headers` reports ARM64 for all four binaries. Test executables were linked but not run because this host is AMD64. JUCE disables `moduleinfo.json` generation because its ARM64 helper cannot run on this host. Steinberg documents this metadata file as optional; a host can query the module factory instead, potentially taking longer to scan. Native ARM64 runtime testing remains outstanding.

The prior Ubuntu 24.04 WSL2 build produced the Linux x64 app and VST3 from the Windows-mounted checkout. Its CTest `MagDrumPluginTests` passed; `MagDrumTests` passed when run directly with the CTest-specified module/test paths but was reported failed without output when launched by CTest. The Linux x64 tarball was assembled and its contents, ELF architecture and linked dependencies inspected. The current Windows environment now returns `Wsl/Service/WSL_E_DISTRO_NOT_FOUND` for Ubuntu, so the Linux test discrepancy cannot presently be rechecked. The package remains a test build pending access to a Linux environment, a native-host smoke test, and resolution of the CTest discrepancy.

The `MagDrumMobilePreview` Windows target also compiled successfully from the shared mobile UI sources. CTest passed both existing suites (2/2), including file-URL import/export copying, cancellation, self-copy protection, and paged database search/filter behavior. This verifies shared C++ code only; iOS document-provider access, AUv3 signing, app-group sharing, and device audio still need an Apple build and device validation.

GitHub Actions attempts for all five configured platforms were blocked by the same billing limit before any job steps began. The local build scripts do not consume Actions minutes. On this Windows computer, x64 is build- and test-verified; ARM64 compiles and packages locally but lacks native runtime testing; WSL reports a missing optional Windows component and needs setup/restart; and both macOS targets need a Mac with Xcode. Thus local builds can replace the hosted process, but this computer alone cannot produce and verify all five platform packages.

Build each package on its required host, then transfer only the finished archives to the Windows release machine and put them in `latest-publish` using the names listed by `Publish-LocalRelease.ps1`. For example, a Mac contributes its Intel/Apple Silicon ZIPs and a Linux host contributes its x64 tarball. Verify each machine's printed SHA-256 after transfer. The publisher calculates checksums again, attaches a checksum file, lists missing platforms, and uploads only the explicitly selected archives through GitHub's Releases API. This upload does not start Actions. The repository workflow is `workflow_dispatch` only, so commits and release tags do not launch its matrix automatically. If GitHub CLI authentication is expired, authenticate once with `gh auth login -h github.com`, then rerun the local publish command:

```powershell
$assets = @('MagDrum-Windows-x64.zip') # Add only packages built and checked on their target hosts.
powershell -NoProfile -ExecutionPolicy Bypass -File .\Publish-LocalRelease.ps1 `
  -Tag v0.1.1-test.N -Asset $assets
```

The package arguments are explicit by design. A release may contain only the platforms available for that test; its generated notes identify the missing packages. Windows x64 is built and test-verified. Windows ARM64 is included as an architecture-checked test package but has not been runtime-tested on ARM64 Windows. Linux x64 is built locally but needs native host smoke testing. macOS Intel and Apple Silicon packages are not present.

## Other platform builds

- **Windows ARM64:** Release v0.1.1-test.113 contains the app and VST3 cross-compiled from this x64 host. Both test executables were cross-compiled, and `dumpbin /headers` reports ARM64 for all four binaries. Tests and runtime still need validation on native ARM64 Windows. JUCE omitted the optional `moduleinfo.json` because its ARM64 helper cannot run on an AMD64 host.
- **Linux x64:** The v0.1.1-test.111 app/VST3 package was built under Ubuntu 24.04 WSL2. The current environment no longer has that Ubuntu distro available (`Wsl/Service/WSL_E_DISTRO_NOT_FOUND`); native Linux smoke testing and the CTest/app-test discrepancy remain open.
- **macOS Intel and Apple Silicon, AU:** use `Build-Unix.sh` on a Mac with compatible Xcode and the required SDKs. Those Apple targets cannot be built on the current Windows host.
- **iOS/iPadOS AUv3:** an initial source target is present, and its shared C++ builds as a Windows desktop preview. The iOS target itself has not been built or packaged. It requires macOS/Xcode, a registered Apple app group, signing setup, and device testing.

**Local flow:** build and test a package on its available host using `Build-Windows.ps1` or `Build-Unix.sh`, transfer finished archives to the release machine, and optionally run `Publish-LocalRelease.ps1` with explicit archive names. To test Windows x64 in Cubase, use `latest-publish/MagDrum-Windows-x64.zip` and follow the bundled installer README; uploading a release is not required for local testing. No Actions minutes are used. Windows ARM64 can be cross-compiled here, but requires native runtime testing. Linux builds require restoring or installing a WSL Ubuntu distro, then resolving the existing CTest discrepancy; Intel and Apple Silicon macOS still need a Mac (Apple Silicon can cross-build Intel). This computer alone cannot produce and verify all five desktop packages. Self-hosted Actions runners are not required. `Publish-LocalRelease.ps1` authenticated through the Git Credential Manager keyring and created releases without starting GitHub Actions jobs.
