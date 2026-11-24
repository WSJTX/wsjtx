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
compiler_signature_helper=/usr/local/share/wsjtx-ci/linux-ccache-compiler-signature.sh

case "$flavor" in
  normal)
    compiler=gcc
    if [ "${WSJTX_CI_IMAGE_PROFILE:-}" = normal-jammy ]; then compiler=gcc-12; fi
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

IFS=$'\t' read -r compiler_version compiler_target compiler_sha256 \
  ccache_compatibility_id compiler_signature_sha256 \
  < <("$compiler_signature_helper" "$compiler")
dpkg_arch="$(dpkg --print-architecture)"
case "$arch:$dpkg_arch" in
  x86_64:amd64|aarch64:arm64|armhf:armhf) ;;
  *)
    echo "Image architecture mismatch: requested $arch, dpkg reports $dpkg_arch" >&2
    exit 1
    ;;
esac
package_sha256="$(dpkg-query -W -f='${binary:Package}=${Version}\n' | LC_ALL=C sort | sha256sum | awk '{print $1}')"
fftw_sha256="$(sha256sum "$LINUX_FFTW_PREFIX/lib/libfftw3f.so.3" "$LINUX_FFTW_PREFIX/lib/libfftw3f_threads.so.3" | sha256sum | awk '{print $1}')"
identity="$recipe_sha256:$compiler_target:$compiler_sha256:$package_sha256:$fftw_sha256"
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
  printf 'fftw_version=%s\n' "$FFTW_VERSION"
  printf 'fftw_source_sha256=%s\n' "$FFTW_SHA256"
  printf 'fftw_sha256=%s\n' "$fftw_sha256"
  printf 'recipe_sha256=%s\n' "$recipe_sha256"
  printf 'toolchain_id=%s\n' "$toolchain_id"
  printf 'ccache_compatibility_id=%s\n' "$ccache_compatibility_id"
  printf 'compiler_signature_sha256=%s\n' "$compiler_signature_sha256"
  if [ "${WSJTX_CI_IMAGE_PROFILE:-}" = normal-jammy ]; then
    . /usr/local/share/wsjtx-ci/qt5-source-config.sh
    . /etc/os-release
    printf 'image_profile=%s\n' "$WSJTX_CI_IMAGE_PROFILE"
    printf 'os_id=%s\nos_version=%s\n' "$ID" "$VERSION_ID"
    printf 'glibc_version=%s\n' "$(getconf GNU_LIBC_VERSION | awk '{print $2}')"
    printf 'cmake_version=%s\n' "$(cmake --version | awk 'NR == 1 {print $3}')"
    printf 'qt_version=%s\n' "$QT5_VERSION"
    printf 'qt_source_sha256=%s\n' "$QT5_SOURCE_SHA256"
    printf 'qt_core_sha256=%s\n' "$(sha256sum /opt/wsjtx/qt/lib/libQt5Core.so.5 | awk '{print $1}')"
  fi
  if [ "$flavor" = normal ]; then
    printf 'hamlib_ref=%s\n' "$LINUX_HAMLIB_REF"
    printf 'hamlib_commit=%s\n' "$LINUX_HAMLIB_COMMIT"
  else
    printf 'hamlib_ref=%s\n' "$TSAN_HAMLIB_REF"
    printf 'hamlib_commit=%s\n' "$TSAN_HAMLIB_COMMIT"
    printf 'hamlib_patch_commit=%s\n' "$TSAN_HAMLIB_PATCH_COMMIT"
    printf 'qt_version=%s\n' "$TSAN_QT_VERSION"
    printf 'boost_version=%s\n' "$TSAN_BOOST_VERSION"
  fi
} > /opt/wsjtx-ci/image.env
