# Third-party notices

This inventory covers the Windows x64 and Linux x64 desktop app and VST3
packages built with the pinned JUCE 8.0.12 source. The package archives contain
the full applicable license texts named below. The macOS build script also
includes the AudioUnit SDK license; those Apple targets still need native build
and runtime validation.

## JUCE Framework 8.0.12

JUCE is used under AGPLv3. Its full license text is `JUCE-LICENSE.md` in each
package. The desktop app and VST3 share JUCE modules for audio basics, devices,
formats, processors and processor headless, audio utilities, core,
cryptography, data structures, DSP, events, graphics, GUI basics and GUI extra.
The VST3 additionally links the JUCE plugin client module.

## Battery `.nbkt` reader

The bounded Battery kit reader is adapted from the Battery format and NI
container readers in [NITools](https://github.com/joanroig/nitools), by Joan
Roig, under GNU GPL version 3. The corresponding complete license text is
included in each package as `GPL-3.0-LICENSE.txt`.

## Bundled components in Windows and Linux packages

| Component | License | Package file |
|---|---|---|
| Steinberg VST3 SDK 3.8.0 | MIT | `VST3-SDK-LICENSE.txt` |
| Xiph FLAC codec | BSD-style | `FLAC-LICENSE.txt` |
| Xiph Ogg Vorbis headers | BSD-style | `Ogg-Vorbis-LICENSE.txt` |
| Xiph libvorbis 1.3.7 | BSD-style | `libvorbis-COPYING.txt` |
| Independent JPEG Group codec | IJG license | `JPEG-LICENSE.txt` |
| PNG Reference Library | libpng license | `PNG-LICENSE.txt` |
| zlib bundled with JUCE | zlib license | `zlib-LICENSE.txt` |
| HarfBuzz | Old MIT license | `HarfBuzz-LICENSE.txt` |
| SheenBidi | Apache License 2.0 | `SheenBidi-LICENSE.txt` |

JUCE enables its bundled FLAC and Ogg Vorbis decoders by default. JPEG and PNG
image codecs, zlib, HarfBuzz and SheenBidi are included through the linked audio,
core and graphics modules. The exact text files are copied from the pinned JUCE
source at package-build time.

The macOS AU package additionally includes `AudioUnit-SDK-LICENSE.txt` from
JUCE's vendored AudioUnit SDK. Its Mac and iOS/AUv3 packages still require an
Apple-platform review before distribution.

## Other components and exclusions

Windows packages also include the Microsoft Visual C++ v14 Redistributable matching the compiler toolset used to build MagDrum. The installer checks the target runtime version and installs this signed prerequisite only when it is missing or older. Redistribution is subject to the Microsoft Visual Studio license terms; see [Redistribute Visual C++ files](https://learn.microsoft.com/cpp/windows/redistributing-visual-cpp-files).

MagDrum fetches SQLite 3.54.1, which is in the public domain. SQLite's source
contains the applicable notice. Linux packages dynamically use host-provided
audio, windowing, graphics and font libraries; these system libraries are not
bundled in the archive.

JUCE's browser and curl support are disabled. AAX, ASIO, LV2, Oboe, CHOC,
Projucer and example-only dependencies are not linked into these Windows/Linux
products. The VST3 SDK's nested license files are copies of the same MIT license
as its top-level license, so one full copy is included.
