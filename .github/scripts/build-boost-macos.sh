#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 4 ]; then
  echo "Usage: build-boost-macos.sh VERSION ARCH DEPLOYMENT_TARGET PREFIX" >&2
  exit 2
fi

version="$1"
arch="$2"
deployment_target="$3"
prefix="$4"
version_underscores=${version//./_}
boost_arch=x86

if [ "$arch" = "arm64" ]; then
  boost_arch=arm
fi

curl -L --fail --retry 5 --retry-delay 10 -o boost.tar.bz2 "https://archives.boost.io/release/${version}/source/boost_${version_underscores}.tar.bz2"
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
