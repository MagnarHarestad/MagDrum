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

## Other components

The SQLite 3.54.1 amalgamation fetched by CMake is in the public domain. JUCE
bundles additional components under their own licenses. Its VST3 SDK copy, for
example, has an MIT license. JUCE's [`LICENSE.md`](https://github.com/juce-framework/JUCE/blob/8.0.12/LICENSE.md)
lists its bundled dependencies and their license files. A dependency's license
and notices remain applicable; MagDrum's AGPLv3 does not replace them.

Before a stable multi-platform distribution, finish a target-by-target audit of
the exact JUCE modules and SDKs included in each package, and include all required
third-party notices. Some optional JUCE components, such as AAX or ASIO, have
separate terms and are not part of the current Windows VST3 package.
