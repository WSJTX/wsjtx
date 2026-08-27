#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 2 ]; then
  echo "Usage: build-hamlib-linux.sh REF PREFIX" >&2
  exit 2
fi

ref="$1"
prefix="$2"

git init hamlib-src
git -C hamlib-src remote add origin https://github.com/Hamlib/Hamlib.git
git -C hamlib-src fetch --depth 1 origin "$ref"
git -C hamlib-src checkout --detach FETCH_HEAD
actual_commit="$(git -C hamlib-src rev-parse HEAD)"
if [[ "$ref" =~ ^[0-9a-f]{40}$ ]] && [ "$actual_commit" != "$ref" ]; then
  echo "Hamlib source mismatch: expected $ref, got $actual_commit" >&2
  exit 1
fi
cd hamlib-src
./bootstrap
./configure \
  --prefix="$prefix" \
  --disable-shared --enable-static \
  --without-cxx-binding \
  --without-readline \
  --without-indi \
  CFLAGS="-g -O2 -fPIC -fdata-sections -ffunction-sections" \
  LDFLAGS="-Wl,--gc-sections"
make -j"$(nproc)"
make install
