MagDrum test package for Windows (x64)

The package contains the standalone MagDrum app and the MagDrum VST3 instrument. The VST3 bundle includes a matching-architecture MagDrum.exe helper used by its instrument-scanner button.
The package also contains the Microsoft Visual C++ v14 Redistributable required by the app and plug-in. The installer checks the installed runtime version and launches the bundled, Microsoft-signed installer only when the matching x64 or ARM64 runtime is missing or older. A UAC prompt may appear if Windows needs to update this system prerequisite.
To install the system-wide VST3 for Cubase, close Cubase and double-click
Install-MagDrum.cmd. Approve the normal Windows UAC prompt; the elevated
installer asks before replacing MagDrum and keeps the previous bundle in backup.
If UAC is cancelled or installation fails, the command window remains open with
the error. You can also run Launch-Install-MagDrum.ps1 from PowerShell.

For a no-UAC per-user install, double-click Install-MagDrum-User.cmd. It installs
to %LOCALAPPDATA%\Programs\Common\VST3 and keeps the previous user copy in
%LOCALAPPDATA%\MagDrum\VST3-backups. VST3 hosts prioritize this user location
before the global Program Files location. If a system-wide MagDrum copy exists,
the script requests one UAC approval to move older MagDrum bundles to
%ProgramData%\MagDrum\VST3-backups. Future per-user updates need no UAC once the
system-wide copy is removed. Use -SkipSystemCleanup to install per-user without
requesting that one-time cleanup. The no-UAC path requires the matching Visual
C++ runtime to already be installed; it stops with an explanation if it is not.

For the system-wide VST3, double-click Install-MagDrum.cmd. It uses standard
Windows UAC and asks before replacing the existing plug-in. The previous bundle
is preserved in %ProgramData%\MagDrum\VST3-backups. You can also run
Install-MagDrum.ps1 -AllUsers from elevated PowerShell; use -Force only when the
replacement has been explicitly authorized. Both installers leave other
vendors' plugins untouched.

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
