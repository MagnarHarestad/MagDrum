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

The Windows x64 release `v0.1.2-test.201` was audited from the actual ZIP:
the app, VST3 and matching helper, installer scripts, project and third-party
license texts, and Microsoft runtime were present. The packaged app and VST3
both report product version `0.1.2.201`; the runtime signature is valid.
`tools/Test-WindowsPackage.ps1` now repeats these checks for each x64 package.
Package SHA-256: `dbd6d8d6a93199acdbd92028388e61d5c5f36be381871e037b790f0cb2234e3c`. The installed VST3 module SHA-256 is `ae71fe82a5aece4e5ce2a604c49b5671b2038f6fa84a44a6ac02abc7c7615c87`; it matches the module in the ZIP.

The current Windows x64 release `v0.1.2-test.252` was checked from
`latest-publish/MagDrum-Windows-x64.zip` with `tools/Test-WindowsPackage.ps1`.
The check passed for the required application, VST3, matching helper,
installer files, AGPLv3 and bundled third-party notices, and signed Microsoft
runtime. The app and VST3 report product version `0.1.2.252`. ZIP SHA-256:
`9b2a0e96add6474ccbf1c45193fc10ab97794ffef2d84171504e7ec2e572903d`. The
installed VST3 module SHA-256 is
`f7c38795cef7cfcbd94540985060e476af1f9faab9f4c1b81f6d5a2aec4d5a96` and
matches the module in the release ZIP.

MSVC-built Windows x64 and ARM64 packages include the matching Microsoft Visual
C++ v14 Redistributable (vc_redist.x64.exe or vc_redist.arm64.exe). The
installer compares its version with the installed target-architecture runtime
and invokes the bundled, signed prerequisite only when needed. Microsoft limits
redistribution to licensed Visual Studio users and applies its own license
terms; see [Redistribute Visual C++ files](https://learn.microsoft.com/cpp/windows/redistributing-visual-cpp-files)
and [latest supported downloads](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist).

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
