#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 3 ]; then
  echo "Usage: build-hamlib-macos.sh BRANCH DEPLOYMENT_TARGET PREFIX" >&2
  exit 2
fi

branch="$1"
deployment_target="$2"
prefix="$3"

git clone --depth 1 --branch "$branch" \
  https://github.com/Hamlib/Hamlib.git hamlib-src
cd hamlib-src
./bootstrap
./configure \
  --prefix="$prefix" \
  --disable-shared --enable-static \
  --without-cxx-binding \
  CFLAGS="-mmacosx-version-min=${deployment_target}" \
  LDFLAGS="-mmacosx-version-min=${deployment_target}"
make -j"$(sysctl -n hw.ncpu)"
make install
