MagDrum test package for Windows (x64)

The package contains the standalone MagDrum app and the MagDrum VST3 instrument.
To install the VST3 for Cubase, close Cubase and run Install-MagDrum.ps1 from
PowerShell. The script installs to the standard system VST3 folder:
C:\Program Files\Common Files\VST3. Windows may ask for administrator access.

Then open Cubase 15 and choose Studio > VST Plug-in Manager > Rescan All.
Add MagDrum as an instrument track and route a MIDI track to it. The plugin
loads WAV, AIFF or FLAC samples that you select in its editor. This is an
unsigned test build; Windows may show a security prompt because the package
is not code-signed. The app and plugin are separate: the plugin is for Cubase,
and the standalone app is the sample library/scanner.
