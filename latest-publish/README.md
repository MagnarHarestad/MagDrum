# Latest MagDrum packages

The current Windows x64 package is [MagDrum v0.1.1-test.42](https://github.com/MagnarHarestad/MagDrum/releases/tag/v0.1.1-test.42). It contains the standalone app, VST3 instrument, and PowerShell installer for Cubase Pro 15 testing.

The desktop sample browser uses debounced background SQLite searches and fetches matching results in 250-sample pages, including text/tag, category, source/kit, feature, review, and sort filters. The archive is built locally with `Build-Windows.ps1`; both CTest suites passed before packaging. Its SHA-256 is `580a51f3ac71edc6bdc7d627de61b8271d62486bfd4bbba1317acc13466a8d63`.

The release currently contains Windows x64 only. Windows ARM64, macOS Intel, macOS Apple Silicon, Linux x64, and iOS/AUv3 packages have not been produced. See [local build requirements](../docs/local-builds.md).

Generated archives are ignored in this directory; this file is the checked-in package index.
