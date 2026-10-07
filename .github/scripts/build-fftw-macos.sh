#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 3 ]; then
  echo "Usage: build-fftw-macos.sh VERSION DEPLOYMENT_TARGET PREFIX" >&2
  exit 2
fi

version="$1"
deployment_target="$2"
prefix="$3"

architecture="${ARCH:-$(uname -m)}"
case "$architecture" in
  x86_64)
    simd_flags=(--enable-sse2 --enable-avx)
    ;;
  arm64)
    simd_flags=(--enable-neon)
    ;;
  *)
    echo "Unsupported macOS architecture: $architecture" >&2
    exit 2
    ;;
esac

curl -L --fail --retry 5 --retry-delay 10 -o fftw.tar.gz "https://www.fftw.org/fftw-${version}.tar.gz"
tar -xzf fftw.tar.gz
cd "fftw-${version}"

./configure \
  --prefix="$prefix" \
  --enable-single \
  --enable-threads \
  --enable-shared --disable-static \
  "${simd_flags[@]}" \
  CFLAGS="-O3 -mmacosx-version-min=${deployment_target}" \
  LDFLAGS="-mmacosx-version-min=${deployment_target}"
make -j"$(sysctl -n hw.ncpu)"
make install
