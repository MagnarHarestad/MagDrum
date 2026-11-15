# MagDrum local test packages

Release candidate `v0.1.1-test.155` contains Windows x64 and Windows ARM64 packages. Linux is omitted because WSL access was unavailable for a fresh build; the old Linux archive is not included.

- Windows x64 SHA-256: `6ba26e3dece4b35cbc3dd83631b99552035c826a0bf02342fa6b02ef6448fdbb`
- Windows ARM64 SHA-256: `b2217aad43d14181c9692784c0d8b2a93a5d65ac302f66d702642af5bdadf744`

The x64 Release CTest suite passes 2/2. Windows ARM64 app, VST3 and test binaries were cross-compiled and architecture-checked, but the tests were not run on native ARM64 hardware. The Windows x64 and ARM64 ZIPs contain the app, VST3, scanner helper, installer, license and third-party notices. The installer backs up an older VST3 outside Cubase's scan paths before replacing it.

This source adds opt-in metadata folder sync in the standalone app and VST. It transfers tags, manual categories and MIDI maps only; it does not copy audio, paths or plug-in state. Database merge is transactional. If a provider reports a conflict, the user must confirm that upload completed before the local snapshot is advanced.

macOS Intel/Apple Silicon and iOS/AUv3 need Apple hardware and SDKs. The public site and the latest release still need to be updated after publication.
