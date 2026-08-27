#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 1 ]; then
  echo "Usage: build-boost-tsan-linux.sh PREFIX" >&2
  exit 2
fi

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
# shellcheck source=.github/scripts/tsan-linux-deps-config.sh
. "${script_dir}/tsan-linux-deps-config.sh"

prefix=$1
tsan_require_toolchain

if [ -e "$prefix" ] && [ -n "$(ls -A "$prefix")" ]; then
  echo "Boost TSan prefix already exists and is not empty: $prefix" >&2
  exit 1
fi

build_root="$(mktemp -d "${RUNNER_TEMP:-/tmp}/wsjtx-boost-tsan.XXXXXX")"
archive="${build_root}/boost.tar.bz2"
version_underscores=${TSAN_BOOST_VERSION//./_}

mkdir -p "$prefix"
curl -L --fail --retry 5 --retry-delay 10 \
  -o "$archive" "$TSAN_BOOST_SOURCE_URL"

actual_sha256="$(sha256sum "$archive" | awk '{print $1}')"
if [ "$actual_sha256" != "$TSAN_BOOST_SOURCE_SHA256" ]; then
  echo "Boost source SHA-256 mismatch" >&2
  echo "Expected: $TSAN_BOOST_SOURCE_SHA256" >&2
  echo "Actual:   $actual_sha256" >&2
  exit 1
fi

tar -xjf "$archive" -C "$build_root"
cd "${build_root}/boost_${version_underscores}"

printf 'using gcc : %s : %s ;\n' "$TSAN_GCC_SUFFIX" "$TSAN_CXX" > user-config.jam
./bootstrap.sh --prefix="$prefix" --with-libraries=log
./b2 -j"$(nproc)" \
  --user-config="${PWD}/user-config.jam" \
  toolset="gcc-${TSAN_GCC_SUFFIX}" \
  variant=release \
  link=shared \
  runtime-link=shared \
  threading=multi \
  cxxstd=11 \
  cxxflags="$TSAN_SANITIZER_FLAGS" \
  linkflags="$TSAN_LINK_FLAGS" \
  --layout=system \
  install

tsan_write_dependency_manifest boost "$prefix"
