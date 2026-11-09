MagDrum test package for Windows (x64)

The package contains the standalone MagDrum app and the MagDrum VST3 instrument.
To install the VST3 for Cubase, close Cubase and run Install-MagDrum.ps1 from
PowerShell. By default the script installs to the standard per-user VST3 folder:
%LOCALAPPDATA%\Programs\Common\VST3. No administrator access is needed.
If a system-wide MagDrum VST3 already exists, a per-user installation prints a
warning because Cubase may find both copies. To replace the system-wide copy,
open PowerShell as administrator and run Install-MagDrum.ps1 -AllUsers. The
script asks before replacing the existing copy and preserves it as a backup.

Then open Cubase 15 and choose Studio > VST Plug-in Manager > Rescan All.
Add MagDrum as an instrument track and route a MIDI track to it. The plugin
loads WAV, AIFF or FLAC samples that you select in its editor. This is an
unsigned test build; Windows may show a security prompt because the package
is not code-signed. The app and plugin are separate: the plugin is for Cubase,
and the standalone app is the sample library/scanner.
