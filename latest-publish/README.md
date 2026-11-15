# MagDrum local test packages

Public test release `v0.1.1-test.156` contains Windows x64, Windows ARM64 and Linux x64 packages.

- Windows x64 SHA-256: `6ba26e3dece4b35cbc3dd83631b99552035c826a0bf02342fa6b02ef6448fdbb`
- Windows ARM64 SHA-256: `b2217aad43d14181c9692784c0d8b2a93a5d65ac302f66d702642af5bdadf744`
- Linux x64 SHA-256: `aa8001315ae5d6d688fbfb4d181152550c5f011fa73b14d679b7ab63a419e1d4`

Release CTest passes 2/2 on Windows x64 and Linux x64. Windows ARM64 app, VST3 and test binaries were cross-compiled and architecture-checked; tests were not run on native ARM64 hardware. Linux x64 passed the Ardour 8 VST3 scanner check, and package executables have no unresolved `ldd` dependencies. Interactive DAW sessions and physical audio-device testing remain.

The Windows packages contain the standalone app, VST3, scanner helper, installer, license and third-party notices. The Linux package contains the app, VST3, scanner helper, installer and notices. The Windows installer backs up an older VST3 outside Cubase's scan paths before replacing it.

This source adds opt-in metadata folder sync in the standalone app and VST. It transfers tags, manual categories and MIDI maps only; it does not copy audio, paths or plug-in state. Database merge is transactional. If a provider reports a conflict, the user must confirm that upload completed before the local snapshot is advanced.

macOS Intel/Apple Silicon and iOS/AUv3 need Apple hardware and SDKs. The public release and [website](https://magnarharestad.github.io/MagDrum-Website/) have been updated.
