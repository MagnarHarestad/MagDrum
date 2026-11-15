#!/usr/bin/env bash
set -euo pipefail

archive=${1:?Usage: Test-LinuxInstaller.sh path/to/MagDrum-Linux-x64.tar.gz}
temp_root=$(mktemp -d)
trap 'rm -rf -- "$temp_root"' EXIT
package_dir="$temp_root/package"
home_dir="$temp_root/home"
mkdir -p -- "$package_dir" "$home_dir"
tar -xzf "$archive" -C "$package_dir"

[[ -x "$package_dir/MagDrum" ]]
[[ -x "$package_dir/Install-MagDrum-Linux.sh" ]]

install_once() {
  HOME="$home_dir" \
    XDG_DATA_HOME="$home_dir/data" \
    MAGDRUM_BIN_DIR="$home_dir/bin" \
    MAGDRUM_VST3_DIR="$home_dir/vst3" \
    "$package_dir/Install-MagDrum-Linux.sh"
}

install_once
[[ -x "$home_dir/bin/MagDrum" ]]
[[ -f "$home_dir/vst3/MagDrum.vst3/Contents/x86_64-linux/MagDrum.so" ]]

install_once
[[ "$(find "$home_dir/data/MagDrum/VST3-backups" -maxdepth 1 -type d -name 'MagDrum.vst3.backup-*' | wc -l)" -eq 1 ]]
[[ "$(find "$home_dir/vst3" -maxdepth 1 -name 'MagDrum.vst3.backup-*' | wc -l)" -eq 0 ]]

echo 'Linux installer test passed: install and update succeed; prior VST3 remains outside the scan path.'
