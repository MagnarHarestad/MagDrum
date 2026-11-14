#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
host_os="$(uname -s)"
host_arch="$(uname -m)"
requested_arch="${1:-}"

if [[ "$host_os" != "Darwin" && "$host_os" != "Linux" ]]; then
  echo "Run this script on a native Linux or macOS machine (or inside a configured WSL distribution)." >&2
  exit 2
fi

if [[ "$host_os" == "Darwin" ]]; then
  case "$requested_arch" in
    "") arch="$host_arch" ;;
    arm64|x86_64) arch="$requested_arch" ;;
    *) echo "Usage: $0 [arm64|x86_64]" >&2; exit 2 ;;
  esac
  case "$arch" in
    arm64|x86_64) ;;
    *) echo "Unsupported macOS architecture: $arch" >&2; exit 2 ;;
  esac
  platform="macOS-$([[ "$arch" == "arm64" ]] && echo AppleSilicon || echo Intel)"
  build_dir="$repo_root/build/local-macos-$arch"
else
  if [[ -n "$requested_arch" && "$requested_arch" != "x86_64" ]]; then
    echo "Linux packaging currently supports x86_64 only." >&2
    exit 2
  fi
  if [[ "$host_arch" != "x86_64" ]]; then
    echo "Linux packaging currently supports x86_64 hosts only (found $host_arch)." >&2
    exit 2
  fi
  arch="x86_64"
  platform="Linux-x64"
  build_dir="$repo_root/build/local-linux-x64"
fi

for tool in cmake ctest; do
  command -v "$tool" >/dev/null || { echo "$tool is required but was not found on PATH." >&2; exit 1; }
done

if [[ "$host_os" == "Darwin" ]]; then
  command -v xcodebuild >/dev/null || { echo "Xcode command line tools are required." >&2; exit 1; }
  cmake -S "$repo_root" -B "$build_dir" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_ARCHITECTURES="$arch" \
    -DBUILD_TESTING=ON
  targets=(MagDrum MagDrumPlugin_VST3 MagDrumPlugin_AU MagDrumTests MagDrumPluginTests)
else
  cmake -S "$repo_root" -B "$build_dir" \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTING=ON
  targets=(MagDrum MagDrumPlugin_VST3 MagDrumTests MagDrumPluginTests)
fi

cmake --build "$build_dir" --config Release --parallel "${BUILD_JOBS:-2}" --target "${targets[@]}"
ctest --test-dir "$build_dir" -C Release --output-on-failure

stage="$build_dir/package-local"
rm -rf "$stage"
mkdir -p "$stage"
juce_source="$build_dir/_deps/juce-src"
[[ -f "$juce_source/LICENSE.md" ]] || { echo "JUCE license was not found under the configured build directory: $juce_source" >&2; exit 1; }
cp "$repo_root/LICENSE" "$repo_root/THIRD-PARTY-NOTICES.md" "$stage/"
cp "$juce_source/LICENSE.md" "$stage/JUCE-LICENSE.md"
cp "$juce_source/modules/juce_audio_processors_headless/format_types/VST3_SDK/LICENSE.txt" "$stage/VST3-SDK-LICENSE.txt"
if [[ "$host_os" == "Darwin" ]]; then
  app="$(find "$build_dir" -type d -path '*/MagDrum_artefacts/Release/MagDrum.app' -print -quit)"
  vst="$(find "$build_dir" -type d -path '*/MagDrumPlugin_artefacts/Release/VST3/MagDrum.vst3' -print -quit)"
  au="$(find "$build_dir" -type d -path '*/MagDrumPlugin_artefacts/Release/AU/MagDrum.component' -print -quit)"
  [[ -n "$app" && -n "$vst" && -n "$au" ]] || { echo "Expected app, VST3, and AU bundles were not all produced." >&2; exit 1; }
  ditto "$app" "$stage/MagDrum.app"
  ditto "$vst" "$stage/MagDrum.vst3"
  ditto "$au" "$stage/MagDrum.component"
  cp "$repo_root/installer/README-macOS.txt" "$stage/README.txt"
  archive="$repo_root/latest-publish/MagDrum-$platform.zip"
  mkdir -p "$(dirname "$archive")"
  ditto -c -k --sequesterRsrc --keepParent "$stage" "$archive"
else
  # Avoid selecting package-local/MagDrum from a previous run when rebuilding.
  app="$build_dir/MagDrum_artefacts/Release/MagDrum"
  vst="$(find "$build_dir" -type d -path '*/MagDrumPlugin_artefacts/Release/VST3/MagDrum.vst3' -print -quit)"
  [[ -f "$app" && -x "$app" && -n "$vst" ]] || { echo "Expected Linux app and VST3 bundle were not both produced." >&2; exit 1; }
  cp "$app" "$stage/MagDrum"
  cp -a "$vst" "$stage/MagDrum.vst3"
  cp "$repo_root/installer/README-Linux.txt" "$stage/README.txt"
  archive="$repo_root/latest-publish/MagDrum-$platform.tar.gz"
  mkdir -p "$(dirname "$archive")"
  tar -C "$stage" -czf "$archive" .
fi

if command -v shasum >/dev/null; then
  shasum -a 256 "$archive"
else
  sha256sum "$archive"
fi
echo "Package: $archive"
