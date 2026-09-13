#!/usr/bin/env bash

# Pinned inputs shared by the ARMHF cross-builder and runtime image pair.
# shellcheck disable=SC2034

_armhf_config_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
# shellcheck source=.github/scripts/linux-ci-image-config.sh
. "$_armhf_config_dir/linux-ci-image-config.sh"
unset _armhf_config_dir

ARMHF_CI_IMAGE_SCHEMA=1
ARMHF_CROSSTOOL_NG_VERSION=1.28.0
ARMHF_CROSSTOOL_NG_SHA256=5750e29a2bda5cd8d67900592576b1670a1987a4dcd5e4f6beae09138a1f5699
ARMHF_CROSSTOOL_NG_URL="https://github.com/crosstool-ng/crosstool-ng/releases/download/crosstool-ng-${ARMHF_CROSSTOOL_NG_VERSION}/crosstool-ng-${ARMHF_CROSSTOOL_NG_VERSION}.tar.xz"

ARMHF_TARGET_TRIPLET=arm-linux-gnueabihf
ARMHF_TOOLCHAIN_PREFIX=/opt/wsjtx/armhf-toolchain
ARMHF_SYSROOT=/opt/wsjtx/armhf-sysroot
ARMHF_RUNTIME_PREFIX=/opt/wsjtx/armhf-runtime
ARMHF_QEMU_EXECUTABLE=/usr/local/bin/qemu-arm-static
ARMHF_GCC_VERSION=13.4.0
ARMHF_GLIBC_VERSION=2.36
ARMHF_BINUTILS_VERSION=2.40
ARMHF_LINUX_HEADERS_VERSION=6.1.147
ARMHF_MIN_KERNEL_VERSION=3.2.0

ARMHF_SYSROOT_PACKAGES=(
  libc6-dev libstdc++6 libgfortran5 libgomp1 libatomic1
  libfftw3-dev libboost-log-dev
  qtbase5-dev qttools5-dev qtmultimedia5-dev libqt5serialport5-dev
  libqt5sql5-sqlite libqt5websockets5-dev libqt5multimedia5-plugins
  libusb-1.0-0-dev libudev-dev portaudio19-dev
)

ARMHF_GCC_RUNTIME_LIBRARIES=(
  libatomic.so.1
  libgcc_s.so.1
  libgfortran.so.5
  libgomp.so.1
  libstdc++.so.6
)
