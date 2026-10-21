#!/usr/bin/env bash
set -euo pipefail

# Run inside a fresh arm32v7/debian container with only the package, this
# script, and a writable log directory mounted from the build runner.
if [ "$#" -ne 3 ]; then
  echo "Usage: validate-armhf-deb-install.sh DEB_PATH bookworm|trixie LOG_DIR" >&2
  exit 2
fi

package=$1
distro=$2
log_dir=$3
case "$distro" in
  bookworm|trixie) ;;
  *) echo "Unsupported ARMHF distro: $distro" >&2; exit 2 ;;
esac
case "$package:$log_dir" in
  /*:/*) ;;
  *) echo "Package and log directory must be absolute paths" >&2; exit 2 ;;
esac

unset LD_LIBRARY_PATH LD_PRELOAD LD_AUDIT QT_PLUGIN_PATH QT_QPA_PLATFORM_PLUGIN_PATH
unset QML_IMPORT_PATH QML2_IMPORT_PATH
export PATH=/usr/sbin:/usr/bin:/sbin:/bin
mkdir -p "$log_dir"
exec > >(tee "$log_dir/clean-deb-install.log") 2>&1

# shellcheck source=/dev/null
source /etc/os-release
if [ "${ID:-}" != debian ] || [ "${VERSION_CODENAME:-}" != "$distro" ]; then
  echo "Container must be Debian $distro; found ${ID:-unknown} ${VERSION_CODENAME:-unknown}"
  exit 1
fi
if [ "$(dpkg --print-architecture)" != armhf ]; then
  echo "Container architecture must be armhf"
  exit 1
fi
if [ "$(dpkg-deb -f "$package" Package)" != wsjtx ] ||
  [ "$(dpkg-deb -f "$package" Architecture)" != armhf ]; then
  echo "Expected an armhf wsjtx package: $package"
  exit 1
fi
if dpkg-query -W -f='${db:Status-Status}' wsjtx 2>/dev/null | awk '$0 == "installed" { found=1 } END { exit !found }'; then
  echo "WSJT-X must not be preinstalled in the clean validation container"
  exit 1
fi

export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install --yes --no-install-recommends "$package" xvfb xauth binutils file

dpkg-query -L wsjtx > "$log_dir/installed-paths.txt"
elf_count=0
while IFS= read -r path; do
  [ -f "$path" ] || continue
  metadata=$(file -L -- "$path")
  [[ "$metadata" == *ELF* ]] || continue
  case "$metadata" in
    *"ELF 32-bit"*"ARM, EABI5"*) ;;
    *) echo "Installed package has a non-ARMHF ELF: $metadata"; exit 1 ;;
  esac
  dynamic=$(readelf -dW -- "$path")
  if [[ "$dynamic" == *"(NEEDED)"* ]]; then
    if ! resolved=$(ldd "$path" 2>&1); then
      printf '%s\n%s\n' "Dependency resolution failed for $path" "$resolved"
      exit 1
    fi
    printf '%s\n%s\n' "$path" "$resolved"
    case "$resolved" in
      *"not found"*|*"/opt/wsjtx/"*|*"/work/"*)
        echo "Installed package has unresolved or build-environment dependencies: $path"
        exit 1
        ;;
    esac
  fi
  elf_count=$((elf_count + 1))
done < "$log_dir/installed-paths.txt"
if [ "$elf_count" -eq 0 ] || [ ! -x /usr/bin/wsjtx ]; then
  echo "Installed package contains no usable WSJT-X executable"
  exit 1
fi

smoke_home=$(mktemp -d)
trap 'rm -rf "$smoke_home"' EXIT
mkdir -p "$smoke_home/runtime"
chmod 700 "$smoke_home/runtime"
status=0
env -i PATH="$PATH" HOME="$smoke_home" XDG_RUNTIME_DIR="$smoke_home/runtime" \
  QT_QPA_PLATFORM=xcb \
  timeout --kill-after=5s 45s xvfb-run -a -s "-screen 0 1280x1024x24" \
  /usr/bin/wsjtx --startup-smoke-test --rig-name CI-DEB-STARTUP \
  > "$log_dir/deb-startup-smoke.log" 2>&1 || status=$?
cat "$log_dir/deb-startup-smoke.log"
if [ "$status" -ne 0 ]; then
  echo "Installed DEB startup smoke failed with status $status (124 means timeout)"
  exit 1
fi
if ! awk '/WSJT-X startup smoke test passed/ { found=1 } END { exit !found }' \
  "$log_dir/deb-startup-smoke.log"; then
  echo "Installed DEB exited without the startup-smoke success marker"
  exit 1
fi
echo "Clean $distro ARMHF installation and startup passed ($elf_count ELF files checked)"
