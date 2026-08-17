#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 1 ]; then
  echo "Usage: build-qt-tsan-linux.sh PREFIX" >&2
  exit 2
fi

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
# shellcheck source=.github/scripts/tsan-linux-deps-config.sh
. "${script_dir}/tsan-linux-deps-config.sh"

prefix=$1
tsan_require_toolchain

if [ -e "$prefix" ] && [ -n "$(ls -A "$prefix")" ]; then
  echo "Qt TSan prefix already exists and is not empty: $prefix" >&2
  exit 1
fi

build_root="$(mktemp -d "${RUNNER_TEMP:-/tmp}/wsjtx-qt-tsan.XXXXXX")"
archive="${build_root}/qt-everywhere-opensource-src-${TSAN_QT_VERSION}.tar.xz"

cleanup_build_root() {
  local expected_root=${RUNNER_TEMP:-/tmp}
  if [[ $build_root != "$expected_root"/wsjtx-qt-tsan.* ]]; then
    echo "Refusing to remove unexpected Qt build directory: $build_root" >&2
    return 1
  fi
  rm -rf -- "$build_root"
}

trap cleanup_build_root EXIT
mkdir -p "$prefix"

curl -L --fail --retry 5 --retry-delay 10 \
  -o "$archive" "$TSAN_QT_SOURCE_URL"

actual_sha256="$(sha256sum "$archive" | awk '{print $1}')"
if [ "$actual_sha256" != "$TSAN_QT_SOURCE_SHA256" ]; then
  echo "Qt source SHA-256 mismatch" >&2
  echo "Expected: $TSAN_QT_SOURCE_SHA256" >&2
  echo "Actual:   $actual_sha256" >&2
  exit 1
fi

tar -xf "$archive" -C "$build_root"
rm -f "$archive"
source_dir="${build_root}/qt-everywhere-src-${TSAN_QT_VERSION}"
if [ ! -d "$source_dir" ]; then
  source_dir="${build_root}/qt-everywhere-opensource-src-${TSAN_QT_VERSION}"
fi
if [ ! -d "$source_dir" ]; then
  echo "Could not locate extracted Qt source under $build_root" >&2
  exit 1
fi

cd "$source_dir"
export CC="$TSAN_CC"
export CXX="$TSAN_CXX"
export CFLAGS="$TSAN_SANITIZER_FLAGS"
export CXXFLAGS="$TSAN_SANITIZER_FLAGS"
export LDFLAGS="$TSAN_LINK_FLAGS"

configure_args=(
  -opensource
  -confirm-license
  -release
  -shared
  -prefix "$prefix"
  -sanitize thread
  -force-debug-info
  -no-strip
  "QMAKE_CFLAGS_RELEASE_WITH_DEBUGINFO=-O1 -g1"
  "QMAKE_CXXFLAGS_RELEASE_WITH_DEBUGINFO=-O1 -g1"
  -nomake examples
  -nomake tests
  -qt-zlib
  -qt-libpng
  -qt-libjpeg
  -qt-pcre
  -qt-harfbuzz
  -qt-sqlite
  -no-sql-mysql
  -no-sql-psql
  -openssl-runtime
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
)

./configure "${configure_args[@]}"
make -j"$(nproc)"
make install

actual_version="$("${prefix}/bin/qtpaths" --qt-version)"
if [ "$actual_version" != "$TSAN_QT_VERSION" ]; then
  echo "Qt version mismatch: expected $TSAN_QT_VERSION, got $actual_version" >&2
  exit 1
fi

tsan_write_dependency_manifest qt "$prefix"
cleanup_build_root
trap - EXIT
