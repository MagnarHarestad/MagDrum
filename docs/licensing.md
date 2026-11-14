# MagDrum licensing

The project owner selected the GNU Affero General Public License, version 3
(AGPLv3), for MagDrum's original source code. The complete license text is in
the repository's [`LICENSE`](../LICENSE) file. Copyright for original MagDrum
code: © 2026 Magnar Harestad, except where a file or component states otherwise.

## JUCE

MagDrum pins JUCE 8.0.12 in `CMakeLists.txt` and uses the JUCE Framework under
its AGPLv3 licensing option. JUCE also offers a separate commercial license;
that is not the option selected for this project. See the [JUCE 8.0.12 license
file](https://github.com/juce-framework/JUCE/blob/8.0.12/LICENSE.md) and the
[JUCE 8 EULA](https://juce.com/legal/juce-8-licence/).

## Windows and Linux package audit

The Windows x64 and Linux x64 desktop-app/VST3 packages share pinned JUCE
modules for audio basics, audio devices, audio formats, processors, processor
headless, audio utilities, core, cryptography, data structures, DSP, events,
graphics, GUI basics and GUI extra. The VST3 also links the plugin client module.
Browser and curl support are disabled. The exact JUCE bundled codecs/rendering components and
their package license files are inventoried in
[`THIRD-PARTY-NOTICES.md`](../THIRD-PARTY-NOTICES.md). Package scripts fail if
any required upstream license file is missing.

JUCE's VST3 SDK copy is MIT-licensed. JUCE enables FLAC and Ogg Vorbis decoding
by default; the package includes FLAC, Ogg Vorbis and libvorbis license texts.
It also includes the JUCE-bundled JPEG, PNG, zlib, HarfBuzz and SheenBidi license
texts. SQLite 3.54.1 is in the public domain. Linux uses host-provided audio,
windowing, graphics and font libraries that the archive does not redistribute.

The macOS package script includes the vendored AudioUnit SDK's Apache 2.0
license. The macOS VST3/AU packages and iOS/AUv3 product still require native
builds and a package-specific audit of their actual linked components before
they can be treated as verified distributions. AAX and ASIO are not linked into
the Windows/Linux products. JUCE's full license, which lists its wider bundled
dependencies, remains in every archive; MagDrum's AGPLv3 does not replace any
third-party license terms.

The GitHub source repository and releases are public. Each published test
release links to the corresponding source revision, and package archives include
the project license and applicable third-party notices. Keep the source
available with binary distribution as required by AGPLv3.
