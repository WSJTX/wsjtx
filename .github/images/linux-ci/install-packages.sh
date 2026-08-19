#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 1 ]; then
  echo "Usage: install-packages.sh normal|tsan" >&2
  exit 2
fi

normal_packages=(
  ca-certificates curl git
  build-essential cmake ccache
  libfftw3-dev libboost-log-dev
  qtbase5-dev qttools5-dev qtmultimedia5-dev libqt5serialport5-dev
  libqt5sql5-sqlite libqt5websockets5-dev
  libqt5multimedia5-plugins
  libusb-1.0-0-dev libudev-dev
  autoconf automake libtool pkg-config
  texinfo
  dpkg-dev
  asciidoctor
  rpm
  python3
  file xz-utils xauth xvfb
  portaudio19-dev
)

tsan_packages=(
  ca-certificates curl git xz-utils bzip2 perl python3
  build-essential binutils cmake ccache file make pkg-config xauth xvfb
  gcc-13 g++-13 gfortran-13
  autoconf automake libtool texinfo
  libfftw3-dev libreadline-dev libusb-1.0-0-dev portaudio19-dev
  libasound2-dev libcups2-dev libdbus-1-dev libegl1-mesa-dev
  libfontconfig1-dev libfreetype-dev libgl1-mesa-dev
  libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev
  libpulse-dev libssl-dev libudev-dev
  libx11-dev libx11-xcb-dev libxcb1-dev libxcb-glx0-dev
  libxcb-icccm4-dev libxcb-image0-dev libxcb-keysyms1-dev
  libxcb-randr0-dev libxcb-render-util0-dev libxcb-shape0-dev
  libxcb-shm0-dev libxcb-sync-dev libxcb-xfixes0-dev
  libxcb-xinerama0-dev libxcb-xkb-dev
  libxext-dev libxfixes-dev libxi-dev libxrender-dev
  libxkbcommon-dev libxkbcommon-x11-dev
)

case "$1" in
  normal)
    packages=("${normal_packages[@]}")
    if ! command -v gfortran >/dev/null 2>&1; then
      packages+=(gfortran)
    fi
    ;;
  tsan) packages=("${tsan_packages[@]}") ;;
  *)
    echo "Unsupported Linux CI image package profile: $1" >&2
    exit 2
    ;;
esac

/usr/local/bin/run-apt-get update
/usr/local/bin/run-apt-get install -y --no-install-recommends "${packages[@]}"
/usr/local/bin/run-apt-get clean
rm -rf /var/lib/apt/lists/*
