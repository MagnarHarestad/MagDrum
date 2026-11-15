#!/usr/bin/env bash
set -euo pipefail

home=${HOME:?HOME must be set for a per-user install}
package_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
app_source="$package_dir/MagDrum"
bundle_source="$package_dir/MagDrum.vst3"
if [[ ! -x "$app_source" || ! -d "$bundle_source" || ! -f "$bundle_source/Contents/x86_64-linux/MagDrum.so" ]]; then
    echo "MagDrum app or Linux VST3 bundle is missing from: $package_dir" >&2
    exit 2
fi

data_home=${XDG_DATA_HOME:-"$home/.local/share"}
bin_dir=${MAGDRUM_BIN_DIR:-"$home/.local/bin"}
vst3_dir=${MAGDRUM_VST3_DIR:-"$home/.vst3"}
backup_root="$data_home/MagDrum/VST3-backups"
app_backup_root="$data_home/MagDrum/app-backups"
plugin_destination="$vst3_dir/MagDrum.vst3"
app_destination="$bin_dir/MagDrum"
install_id="$(date +%Y%m%d-%H%M%S)-$$"
staging_plugin="$vst3_dir/.MagDrum.vst3.installing-$install_id"
staging_app="$bin_dir/.MagDrum.installing-$install_id"
plugin_backup=""
app_backup=""

cleanup() {
    status=$?
    trap - EXIT
    rm -rf -- "$staging_plugin"
    rm -f -- "$staging_app"
    if (( status != 0 )); then
        if [[ -n "$app_backup" && ( -e "$app_backup" || -L "$app_backup" ) ]]; then
            rm -f -- "$app_destination"
            mv -- "$app_backup" "$app_destination"
        fi
        if [[ -n "$plugin_backup" && ( -e "$plugin_backup" || -L "$plugin_backup" ) ]]; then
            rm -rf -- "$plugin_destination"
            mv -- "$plugin_backup" "$plugin_destination"
        fi
    fi
    exit "$status"
}
trap cleanup EXIT

mkdir -p -- "$data_home" "$bin_dir" "$vst3_dir" "$backup_root" "$app_backup_root"
cp -a -- "$bundle_source" "$staging_plugin"
cp -- "$app_source" "$staging_app"
chmod 755 "$staging_app"

if [[ -e "$plugin_destination" || -L "$plugin_destination" ]]; then
    plugin_backup="$backup_root/MagDrum.vst3.backup-$install_id"
    mv -- "$plugin_destination" "$plugin_backup"
fi
mv -- "$staging_plugin" "$plugin_destination"

if [[ -e "$app_destination" || -L "$app_destination" ]]; then
    app_backup="$app_backup_root/MagDrum.backup-$install_id"
    mv -- "$app_destination" "$app_backup"
fi
mv -- "$staging_app" "$app_destination"

printf 'Installed MagDrum standalone app: %s\n' "$app_destination"
printf 'Installed MagDrum VST3: %s\n' "$plugin_destination"
if [[ -n "$plugin_backup" ]]; then
    printf 'Previous VST3 preserved outside scan paths: %s\n' "$plugin_backup"
fi
if [[ -n "$app_backup" ]]; then
    printf 'Previous app preserved: %s\n' "$app_backup"
fi
printf 'Restart your DAW and rescan VST3 plug-ins.\n'