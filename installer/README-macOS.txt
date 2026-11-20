MagDrum test package for macOS

This archive contains the standalone MagDrum app, VST3 instrument and AU
instrument for the architecture named by the archive.

Move MagDrum.app to Applications. The app is unsigned; macOS may require you
to approve opening it in Privacy & Security after the first launch.

Close Cubase and other audio hosts before replacing a plug-in. For a per-user
install, copy MagDrum.vst3 to:
~/Library/Audio/Plug-Ins/VST3/

Copy MagDrum.component to:
~/Library/Audio/Plug-Ins/Components/

You can install system-wide by using /Library/Audio/Plug-Ins/VST3/ and
/Library/Audio/Plug-Ins/Components/. macOS may ask for administrator access.
Keep only one active MagDrum copy in each host's plug-in scan locations. Move
older bundles to a backup folder outside those locations before installing.

Reopen the host and rescan its plug-ins. In Cubase choose Studio > VST Plug-in
Manager > Rescan All. Add MagDrum as an instrument and send it MIDI.

The package contains LICENSE, THIRD-PARTY-NOTICES.md, GPL-3.0-LICENSE.txt and license texts for JUCE,
VST3, AudioUnit SDK, FLAC, Ogg Vorbis/libvorbis, JPEG, PNG, zlib, HarfBuzz and
SheenBidi. MagDrum's original source code is licensed under GNU AGPLv3;
third-party components keep their own license terms.
