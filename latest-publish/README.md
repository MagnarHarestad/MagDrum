# MagDrum local test packages

Release candidate `v0.1.1-test.145` packages the FTS5 trigram library-search migration for Windows x64, Windows ARM64 and Linux x64.

- Windows x64 SHA-256: `e7a7068e9acb55d9c6647fd221704bee96bb08b6a2dd57109b032e1973e49c97`
- Windows ARM64 SHA-256: `c87c4421b8a8df6841877ae7a5346c7fba3257e3ec1a1ab98db319775ee6d800`
- Linux x64 SHA-256: `07b38286fb91beb3cdfaa76d35c018f2a2ecd4c3a1a03268322a8c166ce735b8`

Windows x64 and Linux x64 Release CTest both pass 2/2, including a schema-17 migration rebuild and a 250,000-row query-plan assertion. Windows ARM64 app, plugin and tests cross-compile but have not run on native ARM64 hardware. The system VST3 copy remains .135; Windows still reports hidden Cubase15.exe PID 20148, so the new bundle has not been installed.