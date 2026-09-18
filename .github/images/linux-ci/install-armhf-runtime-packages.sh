#!/usr/bin/env bash
set -euo pipefail

packages=(
  ca-certificates curl file git python3 xz-utils
  binutils cmake dpkg-dev rpm
  libatomic1 libstdc++6 libgfortran5 libgomp1
  libfftw3-single3
  libboost-atomic1.74.0 libboost-chrono1.74.0 libboost-filesystem1.74.0
  libboost-log1.74.0 libboost-regex1.74.0 libboost-thread1.74.0
  libqt5widgets5 libqt5network5 libqt5multimedia5 libqt5printsupport5 libqt5test5
  libqt5multimedia5-plugins libqt5serialport5 libqt5sql5-sqlite
  libqt5websockets5 qt5-qmake-bin qtbase5-dev-tools
  libusb-1.0-0 libudev1 libportaudio2
  xauth xvfb
)

/usr/local/bin/run-apt-get update
/usr/local/bin/run-apt-get install -y --no-install-recommends "${packages[@]}"
/usr/local/bin/run-apt-get clean
rm -rf /var/lib/apt/lists/*
