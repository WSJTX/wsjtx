#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 1 ]; then
  echo "Usage: normalize-armhf-sysroot-symlinks.sh SYSROOT" >&2
  exit 2
fi

sysroot=${1%/}
if [ ! -d "$sysroot" ]; then
  echo "ARMHF sysroot does not exist: $sysroot" >&2
  exit 1
fi

# Debian packages may contain absolute library links. Make them relative so an
# x86 linker cannot follow them into the builder host instead of the sysroot.
count=0
while IFS= read -r -d '' link; do
  target="$(readlink -- "$link")"
  case "$target" in
    /*)
      relative="$(realpath --canonicalize-missing \
        --relative-to="$(dirname -- "$link")" "$sysroot$target")"
      ln -sfn -- "$relative" "$link"
      count=$((count + 1))
      ;;
  esac
done < <(find "$sysroot" -type l -print0)

echo "Normalized $count absolute ARMHF sysroot symlinks"
