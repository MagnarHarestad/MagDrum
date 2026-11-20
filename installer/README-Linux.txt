MagDrum test package for Linux x64

This archive contains the standalone MagDrum app and the MagDrum VST3
instrument, plus a per-user installer.

Run ./MagDrum to start the standalone app. Run
./Install-MagDrum-Linux.sh to install or update the standalone app and VST3.
The installer places the app in ~/.local/bin and the plug-in in ~/.vst3.
Previous versions are moved under ~/.local/share/MagDrum, outside plug-in
scan paths. No administrator access is required.

Set MAGDRUM_BIN_DIR, MAGDRUM_VST3_DIR, or XDG_DATA_HOME before running the
installer to customize these per-user locations. Close your DAW before
updating the plug-in.

Reopen the host and rescan its plug-ins. The standalone app and VST3 use the
same local MagDrum sample database.

The package contains LICENSE, THIRD-PARTY-NOTICES.md, GPL-3.0-LICENSE.txt and license texts for JUCE,
VST3, FLAC, Ogg Vorbis/libvorbis, JPEG, PNG, zlib, HarfBuzz and SheenBidi.
MagDrum's original source code is licensed under GNU AGPLv3; third-party
components keep their own license terms. Linux audio, graphics and font
libraries are supplied by the host operating system and are not bundled here.
