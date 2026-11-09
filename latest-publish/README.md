# Latest MagDrum packages

The current Windows x64 package is `MagDrum-Windows-x64.zip`, published as [MagDrum v0.1.1-test.43](https://github.com/MagnarHarestad/MagDrum/releases/tag/v0.1.1-test.43). It contains the standalone app, VST3 instrument, and PowerShell installer for Cubase Pro 15 testing.

The desktop sample browser uses debounced background SQLite searches and fetches matching results in 250-sample pages, including text/tag, category, source/kit, feature, review, and sort filters. Sample scans now persist each analyzed file immediately, allowing interrupted scans to resume by skipping unchanged files. The archive was rebuilt locally with `Build-Windows.ps1`; both CTest suites passed, including a 50,000-row paging check and interrupted-scan recovery. Its SHA-256 is `f3da0354851211e6a7c0242dec65a1f7e64eff69e9e2886a986f724b4130a479`.

The available local archive is Windows x64 only. Windows ARM64, macOS Intel, macOS Apple Silicon, Linux x64, and iOS/AUv3 packages have not been produced. See [local build requirements](../docs/local-builds.md).

Generated archives are ignored in this directory; this file is the checked-in package index.
