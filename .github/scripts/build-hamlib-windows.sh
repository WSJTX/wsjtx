#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 2 ]; then
  echo "Usage: build-hamlib-windows.sh BRANCH PREFIX" >&2
  exit 2
fi

branch="$1"
prefix="$2"
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

git clone --depth 1 --branch "$branch" \
  https://github.com/Hamlib/Hamlib.git hamlib-src
git -C hamlib-src apply --check "$script_dir/../patches/hamlib-funcube-windows-hid.patch"
git -C hamlib-src apply "$script_dir/../patches/hamlib-funcube-windows-hid.patch"
python3 "$script_dir/tests/test_hamlib_funcube_patch.py" hamlib-src
cd hamlib-src
./bootstrap
./configure \
  --prefix="$prefix" \
  --enable-shared --disable-static \
  --without-cxx-binding \
  --without-readline \
  CFLAGS="-DNDEBUG -g -O2 -fdata-sections -ffunction-sections" \
  LDFLAGS="-Wl,--gc-sections"
make -j"$(nproc)"
make install-strip
