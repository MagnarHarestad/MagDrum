# Latest MagDrum packages

The current local Windows x64 package is `MagDrum-Windows-x64.zip`. It contains the standalone app, VST3 instrument, and PowerShell installer for Cubase Pro 15 testing. The last published release is [MagDrum v0.1.1-test.42](https://github.com/MagnarHarestad/MagDrum/releases/tag/v0.1.1-test.42); this freshly rebuilt local archive has not been uploaded as a new release.

The desktop sample browser uses debounced background SQLite searches and fetches matching results in 250-sample pages, including text/tag, category, source/kit, feature, review, and sort filters. The archive was rebuilt locally with `Build-Windows.ps1`; both CTest suites passed, including a 50,000-row paging check. Its SHA-256 is `050b01711fce9cdcd0737f730e219ad6fb179e3d83df2e5573acfa83efeb09b8`.

The available local archive is Windows x64 only. Windows ARM64, macOS Intel, macOS Apple Silicon, Linux x64, and iOS/AUv3 packages have not been produced. See [local build requirements](../docs/local-builds.md).

Generated archives are ignored in this directory; this file is the checked-in package index.
