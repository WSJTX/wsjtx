#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 4 ]; then
  echo "Usage: build-hamlib-macos.sh BRANCH DEPLOYMENT_TARGET PREFIX LIBUSB_PREFIX" >&2
  exit 2
fi

branch="$1"
deployment_target="$2"
prefix="$3"
libusb_prefix="$4"

git clone --depth 1 --branch "$branch" \
  https://github.com/Hamlib/Hamlib.git hamlib-src
cd hamlib-src
./bootstrap
./configure \
  --prefix="$prefix" \
  --disable-shared --enable-static \
  --without-cxx-binding \
  PKG_CONFIG_PATH="${libusb_prefix}/lib/pkgconfig" \
  CPPFLAGS="-I${libusb_prefix}/include" \
  CFLAGS="-mmacosx-version-min=${deployment_target}" \
  LDFLAGS="-L${libusb_prefix}/lib -mmacosx-version-min=${deployment_target} -Wl,-headerpad_max_install_names"
make -j"$(sysctl -n hw.ncpu)"
make install
