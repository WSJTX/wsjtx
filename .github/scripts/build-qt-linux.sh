#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 1 ]; then
  echo "Usage: build-qt-linux.sh PREFIX" >&2
  exit 2
fi

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
# shellcheck source=.github/scripts/qt5-source-config.sh
. "${script_dir}/qt5-source-config.sh"

prefix=$1
test "$("${CXX:-g++-12}" -dumpversion)" = 12

if [ -e "$prefix" ] && [ -n "$(ls -A "$prefix")" ]; then
  echo "Qt prefix already exists and is not empty: $prefix" >&2
  exit 1
fi

build_root="$(mktemp -d "${RUNNER_TEMP:-/tmp}/wsjtx-qt-release.XXXXXX")"
archive="${build_root}/qt-everywhere-opensource-src-${QT5_VERSION}.tar.xz"

cleanup_build_root() {
  local expected_root=${RUNNER_TEMP:-/tmp}
  if [[ $build_root != "$expected_root"/wsjtx-qt-release.* ]]; then
    echo "Refusing to remove unexpected Qt build directory: $build_root" >&2
    return 1
  fi
  rm -rf -- "$build_root"
}

trap cleanup_build_root EXIT
mkdir -p "$prefix"

curl -L --fail --retry 5 --retry-delay 10 \
  -o "$archive" "$QT5_SOURCE_URL"

actual_sha256="$(sha256sum "$archive" | awk '{print $1}')"
if [ "$actual_sha256" != "$QT5_SOURCE_SHA256" ]; then
  echo "Qt source SHA-256 mismatch" >&2
  echo "Expected: $QT5_SOURCE_SHA256" >&2
  echo "Actual:   $actual_sha256" >&2
  exit 1
fi

tar -xf "$archive" -C "$build_root"
rm -f "$archive"
source_dir="${build_root}/qt-everywhere-src-${QT5_VERSION}"
if [ ! -d "$source_dir" ]; then
  source_dir="${build_root}/qt-everywhere-opensource-src-${QT5_VERSION}"
fi
if [ ! -d "$source_dir" ]; then
  echo "Could not locate extracted Qt source under $build_root" >&2
  exit 1
fi

cd "$source_dir"
export CC="${CC:-gcc-12}"
export CXX="${CXX:-g++-12}"

configure_args=(
  -silent
  -opensource
  -confirm-license
  -release
  -shared
  -prefix "$prefix"
  "QMAKE_CC=$CC"
  "QMAKE_CXX=$CXX"
  "QMAKE_LINK=$CXX"
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
echo "Building Qt ${QT5_VERSION}..."
make -j"$(nproc)"
echo "Installing Qt..."
make -s install
echo "Qt build and installation complete."
mkdir -p "$prefix/share/licenses/Qt5"
for license in "$source_dir"/LICENSE* "$source_dir/qtbase"/LICENSE*; do
  [ -f "$license" ] || continue
  install -m 644 "$license" "$prefix/share/licenses/Qt5/"
done
test -n "$(find "$prefix/share/licenses/Qt5" -type f -print -quit)"

actual_version="$("${prefix}/bin/qtpaths" --qt-version)"
if [ "$actual_version" != "$QT5_VERSION" ]; then
  echo "Qt version mismatch: expected $QT5_VERSION, got $actual_version" >&2
  exit 1
fi

printf 'version=%s\nsource_sha256=%s\n' "$QT5_VERSION" "$QT5_SOURCE_SHA256" > "$prefix/.wsjtx-qt-source"
cleanup_build_root
trap - EXIT
