# Latest MagDrum packages

The current Windows x64 app and VST3 build is in `MagDrum-Windows-x64.zip` for GitHub release [v0.1.1-test.104](https://github.com/MagnarHarestad/MagDrum/releases/tag/v0.1.1-test.104). It contains the new native VST Plugin Scanner view. SHA-256: `a051f0fab98735188bbce1f8d7bf15650b5a993935508243127ac92b7dc04aec`.

The current Windows ARM64 app and VST3 package is `MagDrum-Windows-ARM64.zip`, also in release [v0.1.1-test.103](https://github.com/MagnarHarestad/MagDrum/releases/tag/v0.1.1-test.103). SHA-256: `07628f6ccfccc801e074da064a1a2fb8a5b575d48eb0967d08e7f5f155bf334d`. The app, VST3 and both locally built test executables report ARM64 architecture. Runtime and tests remain unverified because this host is x64.

The .104 x64 package includes the standalone app/helper from .103, the rebuilt VST3, installer and third-party license notices. The plugin and test targets were built with Visual Studio 2026 and both CTest suites passed (2/2). ARM64 remains available from .103; its app, VST3 and both test executables report ARM64, but runtime and CTest require a native ARM64 host. The burger-menu language setting refreshes VST labels and persists across launches. English is the initial default; German, French, Spanish, Swedish, Norwegian, Danish and Finnish are available. The new Plugin Scanner view runs saved-kit MIDI capture in a separate worker; plugin discovery and preset selection still use the standalone manager.

Generated archives are ignored in this directory; this file is the checked-in package index.
