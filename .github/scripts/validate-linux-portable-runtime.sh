#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 4 ]; then
  echo "Usage: validate-linux-portable-runtime.sh APPIMAGE ARCHIVE_DIR LOG_DIR VERSION" >&2
  exit 2
fi
appimage=$1
archive_dir=$2
log_dir=$3
version=$4
mkdir -p "$log_dir"
exec > >(tee "$log_dir/runtime.log") 2>&1
started=$SECONDS

# shellcheck source=/dev/null
source /etc/os-release
case "$ID:$VERSION_ID" in
  ubuntu:22.04) alsa_package=libasound2 ;;
  ubuntu:24.04) alsa_package=libasound2t64 ;;
  *) echo "Unsupported portable runtime: $ID $VERSION_ID" >&2; exit 2 ;;
esac
test "$(uname -m)" = x86_64
export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install --yes --no-install-recommends \
  binutils file python3 xauth xvfb \
  "$alsa_package" libcom-err2 libdrm2 libegl1 libexpat1 libfontconfig1 \
  libfreetype6 libfribidi0 libgbm1 libgl1 libgmp10 libharfbuzz0b \
  libjack-jackd2-0 libstdc++6 libusb-1.0-0 libuuid1 libwayland-client0 \
  libx11-xcb1 libxcb-dri2-0 libxcb-dri3-0 zlib1g
setup_seconds=$((SECONDS - started))
dpkg-query -W > "$log_dir/runtime-packages.txt"
cat /etc/os-release
getconf GNU_LIBC_VERSION
sha256sum "$appimage" > "$log_dir/artifacts.sha256"

scripts=$(cd "$(dirname "$0")" && pwd)
# shellcheck source=.github/scripts/linux-artifact-validation.sh
source "$scripts/linux-artifact-validation.sh"
export LINUX_GLIBC_CEILING=2.35
unset LD_LIBRARY_PATH LD_PRELOAD LD_AUDIT QT_PLUGIN_PATH QT_QPA_PLATFORM_PLUGIN_PATH
work_dir=$(mktemp -d)
trap 'rm -rf "$work_dir"' EXIT
cp "$appimage" "$work_dir/wsjtx.AppImage"
chmod +x "$work_dir/wsjtx.AppImage"
cd "$work_dir"
probe_started=$SECONDS

./wsjtx.AppImage --appimage-extract > "$log_dir/extract.log"
validate_linux_application_tree squashfs-root appimage x86_64
run_packaged_appimage_startup_smoke "$work_dir/wsjtx.AppImage" "$log_dir/gui-startup.log"

archives=("$archive_dir"/wsjtx-"$version"-linux-x86_64-*.tar.gz)
sha256sum "${archives[@]}" >> "$log_dir/artifacts.sha256"
"$scripts/smoke-release-tarballs.sh" \
  --platform linux \
  --version "$version" \
  --target linux-x86_64 \
  --groups "$scripts/../../CMake/release-tarballs.txt" \
  --dir "$archive_dir" \
  --ldd 2>&1 | tee "$log_dir/cli-smoke.log"

{
  echo "setup_seconds=$setup_seconds"
  echo "probe_seconds=$((SECONDS - probe_started))"
  echo "total_seconds=$((SECONDS - started))"
  echo "archive_count=${#archives[@]}"
  echo "result=pass"
} | tee "$log_dir/result.txt"
