#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 3 ]; then
  echo "Usage: build-fftw-macos.sh VERSION DEPLOYMENT_TARGET PREFIX" >&2
  exit 2
fi

version="$1"
deployment_target="$2"
prefix="$3"

curl -L --fail --retry 5 --retry-delay 10 -o fftw.tar.gz "https://www.fftw.org/fftw-${version}.tar.gz"
tar -xzf fftw.tar.gz
cd "fftw-${version}"

SIMD_PARAMS="--enable-neon --enable-armv8-cntvct-el0"
if [[ $(uname -m) == "x86_64" ]]; then
  SIMD_PARAMS="--enable-sse2 --enable-avx --enable-avx2"
fi

./configure \
  --prefix="$prefix" \
  --enable-single \
  $SIMD_PARAMS \
  --enable-threads \
  --enable-shared --disable-static \
  CFLAGS="-mmacosx-version-min=${deployment_target}" \
  LDFLAGS="-mmacosx-version-min=${deployment_target}"
make -j"$(sysctl -n hw.ncpu)"
make install
