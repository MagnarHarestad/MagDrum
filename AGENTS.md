# MagDrum project instructions

- The canonical project directory is `C:\Users\Magnar\GitHub\MagDrum`.
- Keep project changes in this repository; do not use the obsolete `MagDrumVST` directory.
- When the user asks to complete and publish project work, commit the finished changes and create the corresponding GitHub release/site update as part of the task.
- The Windows VST3 install location for Cubase testing is `C:\Program Files\Common Files\VST3\MagDrum.vst3`.
- Keep only the current `MagDrum.vst3` in Cubase's scanned VST3 folders. Preserve old bundles under `%LOCALAPPDATA%\MagDrum\VST3-backups` (or `%ProgramData%\MagDrum\VST3-backups` for all-users installs), never beside the active plug-in where Cubase can index them as duplicate versions.
- Do not build, test, package, publish, or otherwise resume Linux/macOS targets unless the user explicitly asks to resume them. Windows x64/Cubase is the active platform priority; mentions of cross-platform roadmap scope do not override this pause.
