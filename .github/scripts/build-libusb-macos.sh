#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 3 ]; then
  echo "Usage: build-libusb-macos.sh VERSION DEPLOYMENT_TARGET PREFIX" >&2
  exit 2
fi

version="$1"
deployment_target="$2"
prefix="$3"

curl -L --fail --retry 5 --retry-delay 10 -o libusb.tar.bz2 "https://github.com/libusb/libusb/releases/download/v${version}/libusb-${version}.tar.bz2"
tar -xjf libusb.tar.bz2
cd "libusb-${version}"

./configure \
  --prefix="$prefix" \
  --enable-shared --disable-static \
  CFLAGS="-mmacosx-version-min=${deployment_target}" \
  LDFLAGS="-mmacosx-version-min=${deployment_target}"
make -j"$(sysctl -n hw.ncpu)"
make install
