#!/usr/bin/env bash
set -euo pipefail

packages=(
  ca-certificates curl file git python3 xz-utils
  binutils cmake dpkg-dev rpm
  libatomic1 libstdc++6 libgfortran5 libgomp1
  libfftw3-single3
  libqt5multimedia5
  libqt5multimedia5-plugins libqt5serialport5 libqt5sql5-sqlite
  libqt5websockets5 qt5-qmake-bin qtbase5-dev-tools
  libusb-1.0-0 libudev1 libportaudio2
  xauth xvfb
)

case "${ARMHF_DISTRO:-bookworm}" in
  bookworm)
    boost_version=1.74.0
    qt_suffix=
    ;;
  trixie)
    boost_version=1.83.0
    qt_suffix=t64
    ;;
  *) echo "Unsupported ARMHF_DISTRO: $ARMHF_DISTRO" >&2; exit 2 ;;
esac
for library in atomic chrono filesystem log regex thread; do
  packages+=("libboost-$library$boost_version")
done
for library in widgets network printsupport test concurrent; do
  packages+=("libqt5${library}5$qt_suffix")
done

/usr/local/bin/run-apt-get update
/usr/local/bin/run-apt-get install -y --no-install-recommends "${packages[@]}"
/usr/local/bin/run-apt-get clean
rm -rf /var/lib/apt/lists/*
