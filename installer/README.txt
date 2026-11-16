MagDrum test package for Windows (x64)

The package contains the standalone MagDrum app and the MagDrum VST3 instrument. The VST3 bundle includes a matching-architecture MagDrum.exe helper used by its instrument-scanner button.
The package also contains the Microsoft Visual C++ v14 Redistributable required by the app and plug-in. The installer checks the installed runtime version and launches the bundled, Microsoft-signed installer only when the matching x64 or ARM64 runtime is missing or older. A UAC prompt may appear if Windows needs to update this system prerequisite.
To install the system-wide VST3 for Cubase, close Cubase and double-click
Install-MagDrum.cmd. Approve the normal Windows UAC prompt; the elevated
installer asks before replacing MagDrum and keeps the previous bundle in backup.
If UAC is cancelled or installation fails, the command window remains open with
the error. You can also run Launch-Install-MagDrum.ps1 from PowerShell.

For a per-user install, run Install-MagDrum.ps1 from PowerShell. It installs to:
%LOCALAPPDATA%\Programs\Common\VST3. The plug-in copy does not need administrator
access; Windows may request UAC if the required Visual C++ runtime is missing or
older than this build.
If a system-wide MagDrum VST3 already exists, a per-user installation stops
instead of creating a duplicate. To replace the system-wide copy, open
PowerShell as administrator and run Install-MagDrum.ps1 -AllUsers. The script
asks before replacing the existing copy. Pass -Force only when that replacement
is authorized. Previous bundles are preserved outside VST3 scan folders under
%LOCALAPPDATA%\MagDrum\VST3-backups for a per-user install or
%ProgramData%\MagDrum\VST3-backups for an all-users install. After installation,
the installer also moves other MagDrum bundles from both standard Windows VST3
scan folders to that backup location; other vendors' plugins are left alone.

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
