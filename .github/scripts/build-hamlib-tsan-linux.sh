#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 1 ]; then
  echo "Usage: build-hamlib-tsan-linux.sh PREFIX" >&2
  exit 2
fi

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
# shellcheck source=.github/scripts/tsan-linux-deps-config.sh
. "${script_dir}/tsan-linux-deps-config.sh"

prefix=$1
tsan_require_toolchain

if [ -e "$prefix" ] && [ -n "$(ls -A "$prefix")" ]; then
  echo "Hamlib TSan prefix already exists and is not empty: $prefix" >&2
  exit 1
fi

build_root="$(mktemp -d "${RUNNER_TEMP:-/tmp}/wsjtx-hamlib-tsan.XXXXXX")"
source_dir="${build_root}/hamlib-src"
mkdir -p "$prefix" "$source_dir"

git -C "$source_dir" init
git -C "$source_dir" remote add origin https://github.com/Hamlib/Hamlib.git
git -C "$source_dir" fetch --depth 1 origin "$TSAN_HAMLIB_COMMIT"
git -C "$source_dir" checkout --detach FETCH_HEAD

actual_commit="$(git -C "$source_dir" rev-parse HEAD)"
if [ "$actual_commit" != "$TSAN_HAMLIB_COMMIT" ]; then
  echo "Hamlib source commit mismatch: expected $TSAN_HAMLIB_COMMIT, got $actual_commit" >&2
  exit 1
fi

git -C "$source_dir" fetch --depth 2 origin "$TSAN_HAMLIB_PATCH_COMMIT"
git -C "$source_dir" cherry-pick --no-commit "$TSAN_HAMLIB_PATCH_COMMIT"

cd "$source_dir"
./bootstrap
./configure \
  --prefix="$prefix" \
  --disable-shared --enable-static \
  --without-indi \
  --without-cxx-binding \
  CC="$TSAN_CC" \
  CFLAGS="${TSAN_SANITIZER_FLAGS} -fPIC -fdata-sections -ffunction-sections" \
  LDFLAGS="${TSAN_LINK_FLAGS} -Wl,--gc-sections"
make -j"$(nproc)"
make install

tsan_write_dependency_manifest hamlib "$prefix"
