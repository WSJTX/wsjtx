#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 2 ]; then
  echo "Usage: build-fftw-macos.sh DEPLOYMENT_TARGET PREFIX" >&2
  exit 2
fi

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=.github/scripts/fftw-version.sh
. "$script_dir/fftw-version.sh"
deployment_target="$1"
prefix="$2"

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

curl -L --fail --retry 5 --retry-delay 10 -o fftw.tar.gz "https://www.fftw.org/fftw-${FFTW_VERSION}.tar.gz"
printf '%s  %s\n' "$FFTW_SHA256" fftw.tar.gz | shasum -a 256 --check
tar -xzf fftw.tar.gz
cd "fftw-${FFTW_VERSION}"

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
