#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
# shellcheck source=macos-qt-cache-key.sh
. "${script_dir}/macos-qt-cache-key.sh"

usage() {
  cat <<'USAGE'
Usage: build-qt-macos.sh --arch ARCH --deployment-target VERSION --prefix PATH

Builds the pinned dynamic Qt 5 source package for the requested macOS tuple.
USAGE
}

arch=""
deployment_target=""
prefix=""

while [ "$#" -gt 0 ]; do
  case "$1" in
    --arch)
      arch="$2"
      shift 2
      ;;
    --deployment-target)
      deployment_target="$2"
      shift 2
      ;;
    --prefix)
      prefix="$2"
      shift 2
      ;;
    --help|-h)
      usage
      exit 0
      ;;
    *)
      echo "Unknown argument: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

if [ -z "$arch" ] || [ -z "$deployment_target" ] || [ -z "$prefix" ]; then
  usage >&2
  exit 2
fi

case "$arch" in
  arm64|x86_64)
    ;;
  *)
    echo "Unsupported macOS Qt architecture: $arch" >&2
    exit 2
    ;;
esac

case "${arch}:${deployment_target}" in
  arm64:11.0|x86_64:10.13)
    ;;
  *)
    echo "Unsupported macOS Qt deployment tuple: arch=${arch}, deployment_target=${deployment_target}" >&2
    exit 2
    ;;
esac

if [ -e "$prefix" ] && [ -n "$(ls -A "$prefix")" ]; then
  echo "Qt install prefix already exists and is not empty: $prefix" >&2
  exit 1
fi

build_root="${RUNNER_TEMP:-${PWD}}/qt-${QT_VERSION}-${arch}-build"
archive="${build_root}/qt-everywhere-opensource-src-${QT_VERSION}.tar.xz"

mkdir -p "$build_root" "$prefix"

echo "Downloading Qt ${QT_VERSION} from ${QT_SOURCE_URL}"
curl -L --fail --retry 5 --retry-delay 10 -o "$archive" "$QT_SOURCE_URL"

actual_sha256="$(macos_qt_sha256_file "$archive")"
if [ "$actual_sha256" != "$QT_SOURCE_SHA256" ]; then
  echo "Qt source SHA-256 mismatch" >&2
  echo "Expected: $QT_SOURCE_SHA256" >&2
  echo "Actual:   $actual_sha256" >&2
  exit 1
fi

tar -xf "$archive" -C "$build_root"
source_dir="${build_root}/qt-everywhere-src-${QT_VERSION}"
if [ ! -d "$source_dir" ]; then
  source_dir="${build_root}/qt-everywhere-opensource-src-${QT_VERSION}"
fi
if [ ! -d "$source_dir" ]; then
  echo "Could not locate extracted Qt source directory under $build_root" >&2
  exit 1
fi
cd "$source_dir"

export MACOSX_DEPLOYMENT_TARGET="$deployment_target"
export QMAKE_MACOSX_DEPLOYMENT_TARGET="$deployment_target"

configure_args=(
  -opensource
  -confirm-license
  -release
  -shared
  -prefix "$prefix"
  -nomake examples
  -nomake tests
  -qt-zlib
  -qt-libpng
  -qt-libjpeg
  -qt-pcre
  -qt-harfbuzz
  -qt-sqlite
  -securetransport
  -skip qt3d
  -skip qtactiveqt
  -skip qtandroidextras
  -skip qtcharts
  -skip qtconnectivity
  -skip qtdatavis3d
  -skip qtdeclarative
  -skip qtdoc
  -skip qtgamepad
  -skip qtgraphicaleffects
  -skip qtimageformats
  -skip qtlocation
  -skip qtlottie
  -skip qtmacextras
  -skip qtnetworkauth
  -skip qtpurchasing
  -skip qtquick3d
  -skip qtquickcontrols
  -skip qtquickcontrols2
  -skip qtquicktimeline
  -skip qtremoteobjects
  -skip qtscript
  -skip qtscxml
  -skip qtsensors
  -skip qtserialbus
  -skip qtspeech
  -skip qtvirtualkeyboard
  -skip qtwayland
  -skip qtwebchannel
  -skip qtwebengine
  -skip qtwebglplugin
  -skip qtwebview
  -skip qtx11extras
  -skip qtxmlpatterns
  "QMAKE_MACOSX_DEPLOYMENT_TARGET=${deployment_target}"
  "QMAKE_APPLE_DEVICE_ARCHS=${arch}"
)

echo "Configuring Qt ${QT_VERSION} for ${arch}, deployment target ${deployment_target}"
./configure "${configure_args[@]}"

jobs="$(sysctl -n hw.ncpu)"
make -j"$jobs"
make install

"${prefix}/bin/qmake" -query
"${prefix}/bin/qmake" -query QT_VERSION | awk -v expected="$QT_VERSION" '
  $0 != expected {
    printf "Expected qmake QT_VERSION %s, got %s\n", expected, $0 > "/dev/stderr"
    exit 1
  }'
