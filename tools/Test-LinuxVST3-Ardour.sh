#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
    echo "Usage: $0 <MagDrum-Linux-x64.tar.gz | MagDrum.vst3>" >&2
    exit 2
fi

input=$1
temp_dir=""
cleanup() {
    if [[ -n "$temp_dir" && -d "$temp_dir" ]]; then
        rm -rf -- "$temp_dir"
    fi
}
trap cleanup EXIT

if [[ -d "$input" && "$input" == *.vst3 ]]; then
    bundle=$input
elif [[ -f "$input" ]]; then
    temp_dir=$(mktemp -d "${TMPDIR:-/tmp}/magdrum-ardour-check.XXXXXXXX")
    tar -xzf "$input" -C "$temp_dir"
    bundle="$temp_dir/MagDrum.vst3"
else
    echo "Input is neither a VST3 bundle nor a release archive: $input" >&2
    exit 2
fi

if [[ ! -d "$bundle" ]]; then
    echo "MagDrum.vst3 was not found at: $bundle" >&2
    exit 2
fi

scanner=$(command -v ardour-vst3-scanner || true)
if [[ -z "$scanner" ]]; then
    for candidate in /usr/lib/ardour*/ardour-vst3-scanner; do
        if [[ -x "$candidate" ]]; then
            scanner=$candidate
            break
        fi
    done
fi
if [[ -z "$scanner" || ! -x "$scanner" ]]; then
    echo "Ardour's ardour-vst3-scanner was not found. Install Ardour 8 or later." >&2
    exit 2
fi

scanner_dir=$(dirname "$scanner")
export LD_LIBRARY_PATH="$scanner_dir${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
if ! output=$("$scanner" --verbose "$bundle" 2>&1); then
    printf '%s\n' "$output" >&2
    exit 1
fi
printf '%s\n' "$output"
if ! grep -Fq 'Found Plugin: MagDrum' <<<"$output" || ! grep -Fq 'name="MagDrum"' <<<"$output"; then
    echo "Ardour did not report the MagDrum instrument class." >&2
    exit 1
fi
printf 'Ardour VST3 scan passed: %s\n' "$bundle"