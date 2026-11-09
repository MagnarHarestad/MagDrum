#!/usr/bin/env bash
set -euo pipefail

if ! command -v apt-get >/dev/null 2>&1; then
  echo "This helper supports Ubuntu/Debian systems that provide apt-get." >&2
  exit 2
fi

if [[ -r /etc/os-release ]]; then
  # shellcheck disable=SC1091
  source /etc/os-release
  if [[ "${ID:-}" != "ubuntu" && "${ID:-}" != "debian" && "${ID_LIKE:-}" != *debian* ]]; then
    echo "This dependency list is intended for Ubuntu (or a compatible Debian derivative)." >&2
    exit 2
  fi
fi

sudo apt-get update
sudo apt-get install --yes \
  build-essential \
  cmake \
  git \
  pkg-config \
  libasound2-dev \
  libjack-jackd2-dev \
  ladspa-sdk \
  libfontconfig1-dev \
  libfreetype-dev \
  libx11-dev \
  libxcomposite-dev \
  libxcursor-dev \
  libxext-dev \
  libxinerama-dev \
  libxrandr-dev \
  libxrender-dev \
  libxi-dev \
  libegl-dev \
  libglu1-mesa-dev \
  mesa-common-dev
