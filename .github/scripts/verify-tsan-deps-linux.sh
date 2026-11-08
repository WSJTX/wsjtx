#!/usr/bin/env bash
set -euo pipefail

usage() {
  echo "Usage: verify-tsan-deps-linux.sh QT_PREFIX BOOST_PREFIX HAMLIB_PREFIX [CMAKE_CACHE [BINARY]]" >&2
}

if [ "$#" -lt 3 ] || [ "$#" -gt 5 ]; then
  usage
  exit 2
fi

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
# shellcheck source=.github/scripts/tsan-linux-deps-config.sh
. "${script_dir}/tsan-linux-deps-config.sh"
# shellcheck source=.github/scripts/linux-boost-validation.sh
. "${script_dir}/linux-boost-validation.sh"

qt_prefix=$1
boost_prefix=$2
hamlib_prefix=$3
cmake_cache=${4:-}
binary=${5:-}

manifest_value() {
  local manifest=$1 key=$2
  awk -F= -v key="$key" '$1 == key {sub(/^[^=]*=/, ""); print; exit}' "$manifest"
}

cmake_cache_value() {
  local cache=$1 key=$2
  awk -F= -v key="$key" '$1 ~ ("^" key ":[^=]+$") {sub(/^[^=]*=/, ""); print; exit}' "$cache"
}

verify_cmake_cache_prefix() {
  local cache=$1 key=$2 prefix=$3 value
  value="$(cmake_cache_value "$cache" "$key")"
  if [[ "$value" != "$prefix"/* ]]; then
    echo "CMake resolved $key outside $prefix: ${value:-<unset>}" >&2
    return 1
  fi
}

verify_manifest() {
  local dependency=$1 prefix=$2 manifest expected_key actual_key
  manifest="${prefix}/.wsjtx-tsan-manifest"
  if [ ! -f "$manifest" ]; then
    echo "Missing $dependency TSan manifest: $manifest" >&2
    return 1
  fi
  if [ "$(manifest_value "$manifest" dependency)" != "$dependency" ]; then
    echo "Unexpected dependency identity in $manifest" >&2
    return 1
  fi
  expected_key="$(tsan_dependency_cache_key "$dependency")"
  actual_key="$(manifest_value "$manifest" cache_key)"
  if [ "$actual_key" != "$expected_key" ]; then
    echo "$dependency TSan cache manifest mismatch" >&2
    echo "Expected: $expected_key" >&2
    echo "Actual:   $actual_key" >&2
    return 1
  fi
}

verify_dynamic_tsan_symbols() {
  local label=$1 library=$2
  if [ ! -f "$library" ]; then
    echo "Missing $label library: $library" >&2
    return 1
  fi
  if ! nm -D "$library" | awk '$0 ~ /__tsan_/ {found=1} END {exit !found}'; then
    echo "$label does not reference the TSan runtime: $library" >&2
    return 1
  fi
}

verify_static_tsan_symbols() {
  local label=$1 library=$2
  if [ ! -f "$library" ] || [ "$(head -c 7 "$library")" != '!<arch>' ]; then
    echo "Missing or invalid $label static archive: $library" >&2
    return 1
  fi
  if ! nm -A --undefined-only "$library" | awk '$0 ~ /__tsan_/ {found=1} END {exit !found}'; then
    echo "$label does not reference the TSan runtime: $library" >&2
    return 1
  fi
}

verify_manifest qt "$qt_prefix"
verify_manifest boost "$boost_prefix"
verify_manifest hamlib "$hamlib_prefix"
validate_linux_boost_patch "$boost_prefix" "$TSAN_BOOST_VERSION"

qt_core="${qt_prefix}/lib/libQt5Core.so.5"
hamlib_archive="${hamlib_prefix}/lib/libhamlib.a"

verify_dynamic_tsan_symbols Qt5Core "$qt_core"
verify_static_tsan_symbols Hamlib "$hamlib_archive"
for component in log log_setup filesystem thread chrono atomic container; do
  if [ ! -f "${boost_prefix}/lib/libboost_${component}.a" ]; then
    echo "Missing Boost.$component static archive" >&2
    exit 1
  fi
done
shopt -s nullglob
for library in "${boost_prefix}"/lib/libboost_*.a; do
  verify_static_tsan_symbols "Boost.${library##*/}" "$library"
done
for library in "${boost_prefix}"/lib/libboost_*.so*; do
  echo "Shared Boost library found in static TSan prefix: $library" >&2
  exit 1
done

for required in \
  "${qt_prefix}/lib/cmake/Qt5Core/Qt5CoreConfig.cmake" \
  "${qt_prefix}/lib/cmake/Qt5Test/Qt5TestConfig.cmake" \
  "${qt_prefix}/lib/cmake/Qt5Multimedia/Qt5MultimediaConfig.cmake" \
  "${qt_prefix}/lib/cmake/Qt5SerialPort/Qt5SerialPortConfig.cmake" \
  "${qt_prefix}/lib/cmake/Qt5WebSockets/Qt5WebSocketsConfig.cmake" \
  "${boost_prefix}/lib/cmake/Boost-${TSAN_BOOST_VERSION}/BoostConfig.cmake" \
  "${boost_prefix}/include/boost/log/core.hpp"; do
  if [ ! -e "$required" ]; then
    echo "Missing cached TSan dependency file: $required" >&2
    exit 1
  fi
done

if [ -n "$cmake_cache" ]; then
  if [ ! -f "$cmake_cache" ]; then
    echo "CMake cache not found: $cmake_cache" >&2
    exit 1
  fi
  verify_cmake_cache_prefix "$cmake_cache" Qt5Core_DIR "$qt_prefix"
  validate_linux_boost_selection "$(dirname "$cmake_cache")" "$boost_prefix"
  verify_cmake_cache_prefix "$cmake_cache" Hamlib_INCLUDE_DIR "$hamlib_prefix"
  verify_cmake_cache_prefix "$cmake_cache" Hamlib_LIBRARY "$hamlib_prefix"
fi

if [ -n "$binary" ]; then
  if [ ! -x "$binary" ]; then
    echo "TSan binary not found or not executable: $binary" >&2
    exit 1
  fi
  linked="$(ldd "$binary")"
  if ! printf '%s\n' "$linked" | awk -v prefix="$qt_prefix" '$0 ~ /libQt5Core/ && index($0, prefix) {found=1} END {exit !found}'; then
    echo "Binary is not using cached TSan Qt: $binary" >&2
    exit 1
  fi
  if printf '%s\n' "$linked" | awk '/libboost_/ {found=1} END {exit !found}'; then
    echo "Binary has a shared Boost dependency: $binary" >&2
    exit 1
  fi
fi

echo "Verified pinned TSan Qt, Boost, and Hamlib prefixes"
