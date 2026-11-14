MagDrum test package for Linux x64

This archive contains the standalone MagDrum app and the MagDrum VST3
instrument.

Run ./MagDrum to start the standalone app. The VST3 bundle is MagDrum.vst3.
For a per-user install, copy the bundle to:
~/.vst3/

For a system-wide install, copy it to /usr/lib/vst3/ or another VST3 path
configured in your audio host. Close the host before replacing an older copy,
and keep backup bundles outside the host's plug-in scan locations.

Reopen the host and rescan its plug-ins. The standalone app and VST3 use the
same local MagDrum sample database.

The package contains LICENSE, THIRD-PARTY-NOTICES.md and license texts for JUCE,
VST3, FLAC, Ogg Vorbis/libvorbis, JPEG, PNG, zlib, HarfBuzz and SheenBidi.
MagDrum's original source code is licensed under GNU AGPLv3; third-party
components keep their own license terms. Linux audio, graphics and font
libraries are supplied by the host operating system and are not bundled here.
