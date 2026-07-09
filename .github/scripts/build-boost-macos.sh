#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 5 ]; then
  echo "Usage: build-boost-macos.sh VERSION SOURCE_SHA256 ARCH DEPLOYMENT_TARGET PREFIX" >&2
  exit 2
fi

version="$1"
source_sha256="$2"
arch="$3"
deployment_target="$4"
prefix="$5"
version_underscores=${version//./_}
boost_arch=x86

if [ "$arch" = "arm64" ]; then
  boost_arch=arm
fi

sha256_file() {
  if command -v shasum >/dev/null 2>&1; then
    shasum -a 256 "$1" | awk '{print $1}'
  elif command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$1" | awk '{print $1}'
  else
    echo "No SHA-256 tool found; expected shasum or sha256sum" >&2
    return 1
  fi
}

curl -L --fail --retry 5 --retry-delay 10 -o boost.tar.bz2 "https://archives.boost.io/release/${version}/source/boost_${version_underscores}.tar.bz2"
actual_sha256="$(sha256_file boost.tar.bz2)"
if [ "$actual_sha256" != "$source_sha256" ]; then
  echo "::error::Boost source SHA-256 mismatch" >&2
  echo "Expected: ${source_sha256}" >&2
  echo "Actual:   ${actual_sha256}" >&2
  exit 1
fi
tar -xjf boost.tar.bz2
cd "boost_${version_underscores}"

./bootstrap.sh --prefix="$prefix" --with-libraries=log
./b2 -j"$(sysctl -n hw.ncpu)" \
  toolset=clang \
  variant=release \
  link=shared \
  runtime-link=shared \
  threading=multi \
  cxxstd=11 \
  cflags="-mmacosx-version-min=${deployment_target}" \
  cxxflags="-mmacosx-version-min=${deployment_target}" \
  linkflags="-mmacosx-version-min=${deployment_target}" \
  architecture="$boost_arch" \
  address-model=64 \
  install
