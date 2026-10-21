#!/usr/bin/env bash
set -euo pipefail

# shellcheck source=.github/scripts/armhf-toolchain-config.sh
. "$(dirname "${BASH_SOURCE[0]}")/armhf-toolchain-config.sh"
# shellcheck source=.github/scripts/linux-artifact-validation.sh
. "$(dirname "${BASH_SOURCE[0]}")/linux-artifact-validation.sh"

if [ "$#" -ne 1 ]; then
  echo "Usage: audit-armhf-cross-build.sh BUILD_DIR" >&2
  exit 2
fi

build_dir=$1
toolchain_prefix=${WSJT_ARMHF_TOOLCHAIN_PREFIX:-/opt/wsjtx/armhf-toolchain}
sysroot=${WSJT_ARMHF_SYSROOT:-/opt/wsjtx/armhf-sysroot}
runtime_prefix=${WSJT_ARMHF_RUNTIME_PREFIX:-/opt/wsjtx/armhf-runtime}
triplet=arm-linux-gnueabihf
readelf="$toolchain_prefix/bin/$triplet-readelf"
object_count=0

while IFS= read -r -d '' object; do
  headers="$("$readelf" -hW "$object")"
  case "$headers" in
    *"Class:"*"ELF32"*"Machine:"*"ARM"*"Version5 EABI"*) ;;
    *)
      echo "Target build contains a non-ARMHF object: $object" >&2
      exit 1
      ;;
  esac
  object_count=$((object_count + 1))
done < <(find "$build_dir" -type f -name '*.o' -print0)
if [ "$object_count" -eq 0 ]; then
  echo "No target objects found below $build_dir" >&2
  exit 1
fi

for tool in qmake moc uic rcc lrelease lconvert; do
  metadata="$(file -L "/usr/lib/qt5/bin/$tool")"
  case "$metadata" in
    *"ELF 64-bit"*"x86-64"*) ;;
    *) echo "Qt host tool is not x86-64: $metadata" >&2; exit 1 ;;
  esac
done
for target in \
  "$sysroot/usr/lib/$triplet/libQt5Core.so.5" \
  "$sysroot/usr/lib/$triplet/libQt5Widgets.so.5" \
  "$sysroot/usr/lib/$triplet/qt5/plugins/platforms/libqxcb.so" \
  "$sysroot/usr/bin/rigctl" \
  "$sysroot/usr/bin/rigctld" \
  "$sysroot/usr/bin/rigctlcom"; do
  metadata="$(file -L "$target")"
  case "$metadata" in
    *"ELF 32-bit"*"ARM, EABI5"*) ;;
    *) echo "Target dependency has the wrong architecture: $metadata" >&2; exit 1 ;;
  esac
done

READELF="$readelf" validate_armhf_elf_tree "$build_dir" "$ARMHF_DISTRO"

for executable in wsjtx jt9 qmap/qmap map65/map65 ft8code wsprd; do
  versions="$("$readelf" --version-info -W "$build_dir/$executable")"
  for runtime in libstdc++.so.6 libgcc_s.so.1 libgfortran.so.5 libgomp.so.1; do
    if [[ "$versions" == *"${runtime%%.so*}"* ]]; then
      test -f "$runtime_prefix/$runtime"
    fi
  done
done
echo "Audited $object_count ARMHF objects and target binaries; Qt host tools are x86-64"
