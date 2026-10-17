#!/usr/bin/env bash
set -euo pipefail

chmod +x /usr/local/share/wsjtx-ci/*.sh
# shellcheck source=.github/scripts/armhf-ci-image-config.sh
. /usr/local/share/wsjtx-ci/armhf-ci-image-config.sh
armhf_gcc_version_matches "$("$ARMHF_TOOLCHAIN_PREFIX/bin/$ARMHF_TARGET_TRIPLET-gcc" -dumpfullversion -dumpversion)"
test "$("$ARMHF_TOOLCHAIN_PREFIX/bin/$ARMHF_TARGET_TRIPLET-gcc" -dumpmachine)" = "$ARMHF_TARGET_TRIPLET"
package_csv="$(IFS=,; echo "${ARMHF_SYSROOT_PACKAGES[*]}")"
mmdebstrap --variant=essential --architectures=armhf \
  --include="$package_csv" --skip=chroot/mount \
  "$ARMHF_DISTRO" "$ARMHF_SYSROOT" http://deb.debian.org/debian
/usr/local/share/wsjtx-ci/normalize-armhf-sysroot-symlinks.sh "$ARMHF_SYSROOT"
test -f "$ARMHF_SYSROOT/lib/arm-linux-gnueabihf/libc.so.6"
test -f "$ARMHF_SYSROOT/lib/ld-linux-armhf.so.3"
mkdir -p /opt/wsjtx-ci
# shellcheck disable=SC2016
dpkg-query --admindir="$ARMHF_SYSROOT/var/lib/dpkg" -W -f='${binary:Package}=${Version}\n' |
  LC_ALL=C sort -u \
  > /opt/wsjtx-ci/armhf-sysroot-packages.txt
test -s /opt/wsjtx-ci/armhf-sysroot-packages.txt
rm -rf "$ARMHF_SYSROOT/var/cache/apt/archives" "$ARMHF_SYSROOT/var/lib/apt/lists"
mkdir -p /tmp/wsjtx-hamlib-build
cd /tmp/wsjtx-hamlib-build
/usr/local/share/wsjtx-ci/build-hamlib-armhf-cross.sh \
  "$LINUX_HAMLIB_COMMIT" "$ARMHF_SYSROOT" "$ARMHF_TOOLCHAIN_PREFIX"
mkdir -p "$ARMHF_RUNTIME_PREFIX"
for library in "${ARMHF_GCC_RUNTIME_LIBRARIES[@]}"; do
  source_path="$("$ARMHF_TOOLCHAIN_PREFIX/bin/$ARMHF_TARGET_TRIPLET-gcc" -print-file-name="$library")"
  test "$source_path" != "$library"
  cp -L "$source_path" "$ARMHF_RUNTIME_PREFIX/$library"
done
test "$("$ARMHF_TOOLCHAIN_PREFIX/bin/$ARMHF_TARGET_TRIPLET-gcc" -print-file-name=libquadmath.so.0)" = libquadmath.so.0
/usr/local/share/wsjtx-ci/build-armhf-toolchain-smoke.sh \
  "$ARMHF_TOOLCHAIN_PREFIX" "$ARMHF_SYSROOT" /opt/wsjtx-ci/toolchain-smoke
rm -rf /tmp/wsjtx-hamlib-build
cp -L /usr/bin/qemu-arm-static "$ARMHF_QEMU_EXECUTABLE"
