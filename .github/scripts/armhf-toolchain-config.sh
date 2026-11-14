#!/usr/bin/env bash

# Debian compiler and ABI policy for the native ARMHF cross-toolchain.
# shellcheck disable=SC2034

ARMHF_TARGET_TRIPLET=arm-linux-gnueabihf
ARMHF_TOOLCHAIN_PREFIX=/opt/wsjtx/armhf-toolchain
ARMHF_GCC_VERSION=12
ARMHF_GLIBC_VERSION=2.36
ARMHF_MIN_KERNEL_VERSION=3.2.0

export ARMHF_DISTRO=${ARMHF_DISTRO:-bookworm}
ARMHF_TIME_BITS=32
ARMHF_ABI_FLAGS=()
case "$ARMHF_DISTRO" in
  bookworm) ;;
  trixie)
    ARMHF_GCC_VERSION=14
    ARMHF_GLIBC_VERSION=2.41
    unset ARMHF_MIN_KERNEL_VERSION
    ARMHF_TIME_BITS=64
    ARMHF_ABI_FLAGS=(-D_TIME_BITS=64 -D_FILE_OFFSET_BITS=64)
    ;;
  *) echo "Unsupported ARMHF_DISTRO: $ARMHF_DISTRO" >&2; return 2 ;;
esac

armhf_gcc_version_matches() {
  [[ "$1" == "$ARMHF_GCC_VERSION".* ]]
}
