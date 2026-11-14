#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 5 ]; then
  echo "Usage: validate-linux-package-install.sh PACKAGE DISTRO ARCH LOG_DIR APPIMAGE" >&2
  exit 2
fi

package=$1
distro=$2
arch=$3
log_dir=$4
appimage=$5
case "$package:$log_dir:$appimage" in
  /*:/*:/*) ;;
  *) echo "Package, log directory and AppImage must be absolute paths" >&2; exit 2 ;;
esac
case "$arch" in
  x86_64) deb_arch=amd64 ;;
  aarch64) deb_arch=arm64 ;;
  *) echo "Unsupported architecture: $arch" >&2; exit 2 ;;
esac

unset LD_LIBRARY_PATH LD_PRELOAD LD_AUDIT QT_PLUGIN_PATH QT_QPA_PLATFORM_PLUGIN_PATH
unset QML_IMPORT_PATH QML2_IMPORT_PATH
export PATH=/usr/sbin:/usr/bin:/sbin:/bin
mkdir -p "$log_dir"
exec > >(tee "$log_dir/clean-install.log") 2>&1
scripts=$(cd "$(dirname "$0")" && pwd)
# shellcheck source=.github/scripts/linux-artifact-validation.sh
source "$scripts/linux-artifact-validation.sh"
# shellcheck source=/dev/null
source /etc/os-release
if [ "${ID:-}-${VERSION_ID:-}" != "$distro" ] || [ "$(uname -m)" != "$arch" ]; then
  linux_validation_error "Expected $distro $arch, found ${ID:-unknown}-${VERSION_ID:-unknown} $(uname -m)"
  exit 1
fi
sha256sum "$package" "$appimage"

case "$distro" in
  ubuntu-24.04|ubuntu-26.10|debian-12)
    [ "$(dpkg --print-architecture)" = "$deb_arch" ]
    [ "$(dpkg-deb -f "$package" Package)" = wsjtx ]
    [ "$(dpkg-deb -f "$package" Architecture)" = "$deb_arch" ]
    if dpkg-query -W -f='${db:Status-Status}' wsjtx 2>/dev/null | awk '$0 == "installed" { found=1 } END { exit !found }'; then
      linux_validation_error "WSJT-X must not be preinstalled"
      exit 1
    fi
    validate_linux_boost_dependencies "$package" "$(dpkg-deb -f "$package" Depends Pre-Depends Recommends)"
    export DEBIAN_FRONTEND=noninteractive
    apt-get update
    apt-get install --yes --no-install-recommends "$package" xvfb binutils file
    dpkg-query -L wsjtx > "$log_dir/installed-paths.txt"
    dpkg-query -W > "$log_dir/installed-packages.txt"
    ;;
  fedora-44)
    [ "$(rpm -qp --qf '%{NAME}' "$package")" = wsjtx ]
    [ "$(rpm -qp --qf '%{ARCH}' "$package")" = "$arch" ]
    if rpm -q wsjtx >/dev/null 2>&1; then
      linux_validation_error "WSJT-X must not be preinstalled"
      exit 1
    fi
    validate_linux_boost_dependencies "$package" "$(rpm -qpR "$package")"
    dnf install --assumeyes "$package" xorg-x11-server-Xvfb binutils file findutils
    rpm -ql wsjtx > "$log_dir/installed-paths.txt"
    rpm -qa > "$log_dir/installed-packages.txt"
    ;;
  *) linux_validation_error "Unsupported distribution: $distro"; exit 2 ;;
esac

elf_count=0
while IFS= read -r path; do
  case "${path##*/}" in
    libboost_*) linux_validation_error "Unexpected installed Boost library: $path"; exit 1 ;;
  esac
  [ -f "$path" ] || continue
  metadata=$(file -L -- "$path")
  [[ "$metadata" == *ELF* ]] || continue
  validate_elf_metadata "$arch" "$path" "$metadata" "$(readelf -hW -- "$path")" "$(readelf -lW -- "$path")"
  dynamic=$(readelf -dW -- "$path")
  validate_linux_boost_dynamic "$path" "$dynamic"
  if [[ "$dynamic" == *"(NEEDED)"* ]]; then
    resolved=$(ldd "$path")
    printf '%s\n%s\n' "$path" "$resolved"
    case "$resolved" in
      *"not found"*|*"/opt/wsjtx/"*|*"/work/"*|*"/packages/"*|*libboost_*)
        linux_validation_error "Unresolved, build-environment or shared Boost dependency: $path"
        exit 1
        ;;
    esac
  fi
  elf_count=$((elf_count + 1))
done < "$log_dir/installed-paths.txt"
if [ "$elf_count" -eq 0 ] || [ ! -x /usr/bin/wsjtx ]; then
  linux_validation_error "Installed package contains no usable WSJT-X executable"
  exit 1
fi

smoke_home=$(mktemp -d)
xvfb_pid=""
cleanup() {
  if [ -n "$xvfb_pid" ]; then kill "$xvfb_pid" 2>/dev/null || true; fi
  rm -rf "$smoke_home"
}
trap cleanup EXIT
mkdir -p "$smoke_home/runtime" "$smoke_home/.qttest/config"
chmod 700 "$smoke_home/runtime"
cat > "$smoke_home/.qttest/config/wsjtx_log_config.ini" <<'EOF'
[Core]
DisableLogging="false"
[Sinks.PackageValidation]
Destination="TextFile"
Asynchronous="true"
AutoFlush="false"
FileName="${HomeLocation}/package-logging.log"
EnableFinalRotation="false"
Format="%Message%"
EOF

Xvfb -displayfd 3 -screen 0 1280x1024x24 -nolisten tcp 3> "$smoke_home/display" > "$log_dir/xvfb.log" 2>&1 &
xvfb_pid=$!
for _ in {1..100}; do
  [ ! -s "$smoke_home/display" ] || break
  kill -0 "$xvfb_pid"
  sleep 0.1
done
[ -s "$smoke_home/display" ] || { linux_validation_error "Xvfb did not become ready"; exit 1; }
display=":$(cat "$smoke_home/display")"

run_startup() {
  local program=$1 label=$2 status=0
  rm -f "$smoke_home/package-logging.log"
  env -i PATH="$PATH" HOME="$smoke_home" XDG_CONFIG_HOME="$smoke_home/.qttest/config" \
    XDG_RUNTIME_DIR="$smoke_home/runtime" DISPLAY="$display" QT_QPA_PLATFORM=xcb \
    APPIMAGE_EXTRACT_AND_RUN=1 \
    timeout --kill-after=5s 45s "$program" --startup-smoke-test --rig-name "CI-$label" \
    > "$log_dir/$label-startup.log" 2>&1 || status=$?
  cat "$log_dir/$label-startup.log"
  [ "$status" -eq 0 ] || { linux_validation_error "$label startup failed with status $status"; return 1; }
  awk '/WSJT-X startup smoke test passed/ { found=1 } END { exit !found }' "$log_dir/$label-startup.log"
  [ -f "$smoke_home/package-logging.log" ] || { linux_validation_error "$label did not write the configured log"; return 1; }
  cp "$smoke_home/package-logging.log" "$log_dir/$label-boost-logging.log"
  awk '/Program startup/ { found=1 } END { exit !found }' "$log_dir/$label-boost-logging.log"
}

run_startup /usr/bin/wsjtx PACKAGE
for program in jt9 wsjt-codec; do
  (cd "$smoke_home" && env -i PATH="$PATH" HOME="$smoke_home" \
    timeout 30s "/usr/bin/$program" --help > "$log_dir/$program-help.log" 2>&1)
done
cp "$appimage" "$smoke_home/wsjtx.AppImage"
chmod +x "$smoke_home/wsjtx.AppImage"
run_startup "$smoke_home/wsjtx.AppImage" APPIMAGE
echo "Clean $distro $arch package, logging, CLI and AppImage startup passed ($elf_count ELF files checked)"
