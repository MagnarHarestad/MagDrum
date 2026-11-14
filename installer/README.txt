MagDrum test package for Windows (x64)

The package contains the standalone MagDrum app and the MagDrum VST3 instrument. The VST3 bundle includes a matching-architecture MagDrum.exe helper used by its instrument-scanner button.
To install the VST3 for Cubase, close Cubase and run Install-MagDrum.ps1 from
PowerShell. By default the script installs to the standard per-user VST3 folder:
%LOCALAPPDATA%\Programs\Common\VST3. No administrator access is needed.
If a system-wide MagDrum VST3 already exists, a per-user installation prints a
warning because Cubase may find both copies. To replace the system-wide copy,
open PowerShell as administrator and run Install-MagDrum.ps1 -AllUsers. The
script asks before replacing the existing copy and preserves previous bundles
outside the VST3 scan folder: under %LOCALAPPDATA%\MagDrum\VST3-backups for a
per-user install or %ProgramData%\MagDrum\VST3-backups for an all-users install.

Then open Cubase 15 and choose Studio > VST Plug-in Manager > Rescan All.
Add MagDrum as an instrument track and route a MIDI track to it. The plugin
loads WAV, AIFF or FLAC samples that you select in its editor. This is an
unsigned test build; Windows may show a security prompt because the package
is not code-signed. The instrument scanner opens as a separate MagDrum window
so third-party instrument crashes remain outside Cubase. It uses the same local
sample database as the VST's sample browser.

The package contains LICENSE, THIRD-PARTY-NOTICES.md and license texts for JUCE,
VST3, FLAC, Ogg Vorbis/libvorbis, JPEG, PNG, zlib, HarfBuzz and SheenBidi.
MagDrum's original source code is licensed under GNU AGPLv3; third-party
components keep their own license terms.
