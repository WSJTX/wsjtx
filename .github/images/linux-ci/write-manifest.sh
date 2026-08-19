#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 4 ]; then
  echo "Usage: write-manifest.sh FLAVOR ARCH GENERATION RECIPE_SHA256" >&2
  exit 2
fi

flavor=$1
arch=$2
generation=$3
recipe_sha256=$4

# shellcheck source=.github/scripts/linux-ci-image-config.sh
. /usr/local/share/wsjtx-ci/linux-ci-image-config.sh

case "$flavor" in
  normal)
    compiler=gcc
    ;;
  tsan)
    # shellcheck source=.github/scripts/tsan-linux-deps-config.sh
    . /usr/local/share/wsjtx-ci/tsan-linux-deps-config.sh
    compiler=gcc-13
    ;;
  *)
    echo "Unsupported Linux CI image flavor: $flavor" >&2
    exit 2
    ;;
esac

compiler_path="$(readlink -f "$(command -v "$compiler")")"
compiler_version="$($compiler -dumpfullversion -dumpversion)"
compiler_sha256="$(sha256sum "$compiler_path" | awk '{print $1}')"
compiler_target="$($compiler -dumpmachine)"
dpkg_arch="$(dpkg --print-architecture)"
case "$arch:$dpkg_arch" in
  x86_64:amd64|aarch64:arm64|armhf:armhf) ;;
  *)
    echo "Image architecture mismatch: requested $arch, dpkg reports $dpkg_arch" >&2
    exit 1
    ;;
esac
package_sha256="$(dpkg-query -W -f='${binary:Package}=${Version}\n' | LC_ALL=C sort | sha256sum | awk '{print $1}')"
identity="$recipe_sha256:$compiler_target:$compiler_sha256:$package_sha256"
toolchain_sha256="$(printf '%s' "$identity" | sha256sum | awk '{print $1}')"
toolchain_id="gcc${compiler_version}-${arch}-${toolchain_sha256:0:20}"

mkdir -p /opt/wsjtx-ci
{
  printf 'schema=%s\n' "$LINUX_CI_IMAGE_SCHEMA"
  printf 'flavor=%s\n' "$flavor"
  printf 'architecture=%s\n' "$arch"
  printf 'generation=%s\n' "$generation"
  printf 'compiler=%s\n' "$compiler"
  printf 'compiler_version=%s\n' "$compiler_version"
  printf 'compiler_sha256=%s\n' "$compiler_sha256"
  printf 'compiler_target=%s\n' "$compiler_target"
  printf 'dpkg_arch=%s\n' "$dpkg_arch"
  printf 'package_sha256=%s\n' "$package_sha256"
  printf 'recipe_sha256=%s\n' "$recipe_sha256"
  printf 'toolchain_id=%s\n' "$toolchain_id"
  if [ "$flavor" = normal ]; then
    printf 'hamlib_ref=%s\n' "$LINUX_HAMLIB_REF"
    printf 'hamlib_commit=%s\n' "$LINUX_HAMLIB_COMMIT"
    printf 'pfunit_version=%s\n' "$LINUX_PFUNIT_VERSION"
    printf 'pfunit_commit=%s\n' "$LINUX_PFUNIT_COMMIT"
  else
    printf 'hamlib_ref=%s\n' "$TSAN_HAMLIB_REF"
    printf 'hamlib_commit=%s\n' "$TSAN_HAMLIB_COMMIT"
    printf 'hamlib_patch_commit=%s\n' "$TSAN_HAMLIB_PATCH_COMMIT"
    printf 'qt_version=%s\n' "$TSAN_QT_VERSION"
    printf 'boost_version=%s\n' "$TSAN_BOOST_VERSION"
  fi
} > /opt/wsjtx-ci/image.env
