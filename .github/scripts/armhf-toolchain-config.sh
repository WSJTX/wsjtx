#!/usr/bin/env bash

# Pinned inputs for the native ARMHF cross-toolchain.
# shellcheck disable=SC2034

ARMHF_TOOLCHAIN_REBUILD_REVISION=1
ARMHF_CROSSTOOL_NG_VERSION=1.28.0
ARMHF_CROSSTOOL_NG_SHA256=5750e29a2bda5cd8d67900592576b1670a1987a4dcd5e4f6beae09138a1f5699
ARMHF_CROSSTOOL_NG_URL="https://github.com/crosstool-ng/crosstool-ng/releases/download/crosstool-ng-${ARMHF_CROSSTOOL_NG_VERSION}/crosstool-ng-${ARMHF_CROSSTOOL_NG_VERSION}.tar.xz"

ARMHF_TARGET_TRIPLET=arm-linux-gnueabihf
ARMHF_TOOLCHAIN_PREFIX=/opt/wsjtx/armhf-toolchain
ARMHF_GCC_VERSION=13.4.0
ARMHF_GLIBC_VERSION=2.36
ARMHF_BINUTILS_VERSION=2.40
ARMHF_LINUX_HEADERS_VERSION=6.1.147
ARMHF_MIN_KERNEL_VERSION=3.2.0

export ARMHF_DISTRO=${ARMHF_DISTRO:-bookworm}
ARMHF_TIME_BITS=32
ARMHF_ABI_FLAGS=()
case "$ARMHF_DISTRO" in
  bookworm) ;;
  trixie)
    ARMHF_GCC_VERSION=14
    ARMHF_GLIBC_VERSION=2.41
    unset ARMHF_BINUTILS_VERSION ARMHF_LINUX_HEADERS_VERSION ARMHF_MIN_KERNEL_VERSION
    ARMHF_TIME_BITS=64
    ARMHF_ABI_FLAGS=(-D_TIME_BITS=64 -D_FILE_OFFSET_BITS=64)
    ;;
  *) echo "Unsupported ARMHF_DISTRO: $ARMHF_DISTRO" >&2; return 2 ;;
esac

armhf_gcc_version_matches() {
  if [ "$ARMHF_DISTRO" = trixie ]; then
    [[ "$1" == "$ARMHF_GCC_VERSION".* ]]
  else
    [ "$1" = "$ARMHF_GCC_VERSION" ]
  fi
}
