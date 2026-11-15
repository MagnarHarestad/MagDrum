# MagDrum local test packages

Release candidate `v0.1.1-test.151` packages the default shared content-root scan for Windows x64, Windows ARM64 and Linux x64.

- Windows x64 SHA-256: `eaf3f88d14f4b664ce98c6e35b259a24204ed113c465a5fc70876557cfdecb1e`
- Windows ARM64 SHA-256: `1696a8cca354b6ba69f4237c0dcfcc8681d6f1cbeeadc7bce8c1a46295188e91`
- Linux x64 SHA-256: `43267411285089f948517f513c87c8f822a84f9b7e23e08688392edf5dd1a473`

Windows x64 and Linux x64 Release CTest both pass 2/2, including the default-folder nesting regression. Windows ARM64 app, VST3 and test binaries cross-compile, but have not run on native ARM64 hardware; the build script now skips CTest execution when cross-compiling on x64. The installed system VST3 remains .135. Windows still reports hidden Cubase15.exe PID 20148, so the new bundle is built and staged but not installed.
