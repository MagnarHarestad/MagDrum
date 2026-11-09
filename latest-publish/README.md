# Latest MagDrum packages

Download the current Windows x64 test package from [MagDrum v0.1.1-test.25](https://github.com/MagnarHarestad/MagDrum/releases/tag/v0.1.1-test.25).

The current package is:

- `MagDrum-Windows-x64.zip` — standalone app, VST3 and PowerShell installer.

The package is intended for testing the VST3 instrument in Cubase Pro 15. Its installer defaults to the per-user VST3 location and preserves an existing MagDrum installation when replacing it. The standalone scanner lists programs exposed by drum instruments and lets users select them before saving kit state or scanning notes. MIDI export and import preserve pattern length, including empty bars, and the step sequencer can render and play a sample-based audio preview with tempo changes and looping. Plugin scans can capture 1–8 repeats, store each take, and suggest velocity-layer and repeated-trigger variation candidates for listening review. The database now keeps stable folder/plugin source records, backfills prior scan folders, and lets the browser filter by a specific folder source. The browser can also filter by measured duration bands and spectral brightness. Manual category corrections update both generated previews and their linked plugin sounds. Structured JSONL logs record app lifecycle and scan outcomes under the user's application data folder. Missing or unreadable samples now produce a clear preview error and a structured warning log. SHA-256: `3a1af2c900244c186b4fabcd3448c1ce9c9413f0f44969050d68539c2614ca45`.

This package was built and tested locally on Windows x64; CTest passed (1/1). Publishing with GitHub CLI uploads the archive without starting Actions runners. Windows ARM64 requires Visual Studio's missing ARM64 C++ component; macOS Intel/Apple Silicon and AU packages must be built on a Mac with Xcode; Linux x64 can be built on Linux or in a configured WSL2 environment. Local build scripts, tested package details, and host requirements are documented in [local-builds.md](../docs/local-builds.md). The other four platform archives have not been produced yet.

Archives generated locally are ignored here; this directory is the in-repository package index.
