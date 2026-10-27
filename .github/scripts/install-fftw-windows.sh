#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 1 ]; then
  echo "Usage: install-fftw-windows.sh PREFIX" >&2
  exit 2
fi

prefix="$1"
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=.github/scripts/fftw-version.sh
. "$script_dir/fftw-version.sh"
work="$(mktemp -d)"
trap 'rm -r "$work"' EXIT
archive="mingw-w64-ucrt-x86_64-fftw-${FFTW_WINDOWS_PACKAGE_VERSION}-any.pkg.tar.zst"
curl --fail --location --retry 5 --retry-delay 10 \
  "https://mirror.msys2.org/mingw/ucrt64/$archive" -o "$work/$archive"
actual_sha256="$(sha256sum "$work/$archive" | awk '{print $1}')"
if [ "$actual_sha256" != "$FFTW_WINDOWS_SHA256" ]; then
  echo "FFTW Windows package SHA-256 mismatch: $actual_sha256" >&2
  exit 1
fi
tar --zstd -xf "$work/$archive" -C "$work" ucrt64
mkdir -p "$prefix"
cp -R "$work/ucrt64/." "$prefix/"
test -f "$prefix/include/fftw3.h"
test -f "$prefix/lib/libfftw3f.dll.a"
test -f "$prefix/lib/libfftw3f_threads.dll.a"
test -f "$prefix/bin/libfftw3f-3.dll"
test -f "$prefix/bin/libfftw3f_threads-3.dll"
