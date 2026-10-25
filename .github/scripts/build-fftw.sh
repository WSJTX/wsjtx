#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -lt 1 ]; then
  echo "Usage: build-fftw.sh PREFIX [configure arguments...]" >&2
  exit 2
fi

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=.github/scripts/fftw-version.sh
. "$script_dir/fftw-version.sh"
prefix=$1
shift

case "${FFTW_ARCH:-$(uname -m)}" in
  x86_64|amd64) simd_flags=(--enable-sse2 --enable-avx) ;;
  aarch64|arm64) simd_flags=(--enable-neon) ;;
  armhf|armv7l) simd_flags=() ;;
  *) echo "Unsupported FFTW architecture: ${FFTW_ARCH:-$(uname -m)}" >&2; exit 2 ;;
esac

work=$(mktemp -d)
trap 'rm -r -- "$work"' EXIT
curl --fail --location --retry 5 --retry-delay 10 \
  "https://www.fftw.org/fftw-${FFTW_VERSION}.tar.gz" -o "$work/fftw.tar.gz"
printf '%s  %s\n' "$FFTW_SHA256" "$work/fftw.tar.gz" | sha256sum -c -
tar -xzf "$work/fftw.tar.gz" -C "$work"
cd "$work/fftw-${FFTW_VERSION}"

export CFLAGS="${CFLAGS:--O3}"
./configure --prefix="$prefix" --enable-single --enable-threads \
  --enable-shared --disable-static "${simd_flags[@]}" "$@"
make -j"${MAKE_JOBS:-$(getconf _NPROCESSORS_ONLN)}"
make install DESTDIR="${DESTDIR:-}"
