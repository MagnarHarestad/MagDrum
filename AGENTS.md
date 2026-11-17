# MagDrum project instructions

- The canonical project directory is `C:\Users\Magnar\GitHub\MagDrum`.
- Keep project changes in this repository; do not use the obsolete `MagDrumVST` directory.
- When the user asks to complete and publish project work, commit the finished changes and create the corresponding GitHub release/site update as part of the task.
- The standard Windows system-wide VST3 location is `C:\Program Files\Common Files\VST3\MagDrum.vst3`. For the current no-UAC Cubase test flow, install per-user at `%LOCALAPPDATA%\Programs\Common\VST3\MagDrum.vst3`; only use the system-wide path for an explicitly requested elevated install.
- Keep only the current `MagDrum.vst3` in Cubase's scanned VST3 folders. Preserve old bundles under `%LOCALAPPDATA%\MagDrum\VST3-backups` (or `%ProgramData%\MagDrum\VST3-backups` for all-users installs), never beside the active plug-in where Cubase can index them as duplicate versions.
- **Platform pause (standing instruction): Linux and macOS work is paused. Do not start or continue Linux/macOS builds, tests, packaging, publishing, CI jobs, install work, or roadmap implementation. Do not present these platforms as active or in progress. Work only on Windows x64/Cubase. Resume Linux/macOS only after the user explicitly asks to resume those platforms. This instruction overrides generic cross-platform roadmap language.**
- Do not update Linux/macOS documentation or build configuration merely to restate this pause; leave historical platform material untouched until the user explicitly asks to resume those platforms.
