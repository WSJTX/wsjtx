#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 2 ]; then
  echo "Usage: build-boost-windows.sh WIN32_WINNT PREFIX" >&2
  exit 2
fi

win32_winnt="$1"
prefix="$2"
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=.github/scripts/boost-version.sh
. "$script_dir/boost-version.sh"
version_underscores=${BOOST_VERSION//./_}
api_floor_flags="-D_WIN32_WINNT=${win32_winnt} -DWINVER=${win32_winnt} -DBOOST_USE_WINAPI_VERSION=${win32_winnt}"
install_prefix=$(cygpath -m "$prefix")

sha256_file() {
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$1" | awk '{print $1}'
  elif command -v shasum >/dev/null 2>&1; then
    shasum -a 256 "$1" | awk '{print $1}'
  else
    echo "No SHA-256 tool found; expected sha256sum or shasum" >&2
    return 1
  fi
}

curl -L --fail --retry 5 --retry-delay 10 -o boost.tar.bz2 "$BOOST_SOURCE_URL"
actual_sha256="$(sha256_file boost.tar.bz2)"
if [ "$actual_sha256" != "$BOOST_SOURCE_SHA256" ]; then
  echo "::error::Boost source SHA-256 mismatch" >&2
  echo "Expected: ${BOOST_SOURCE_SHA256}" >&2
  echo "Actual:   ${actual_sha256}" >&2
  exit 1
fi
tar -xjf boost.tar.bz2
cd "boost_${version_underscores}"
python3 "$script_dir/patch-boost-log-queue.py" . \
  --manifest "${install_prefix}/share/wsjtx-build/boost-log-queue-patch.json"

python3 - <<'PY'
from pathlib import Path

platform = Path("boost/atomic/detail/platform.hpp")
text = platform.read_text()
old = """#if defined(BOOST_WINDOWS)

#define BOOST_ATOMIC_DETAIL_WAIT_BACKEND windows

#else // defined(BOOST_WINDOWS)
"""
new = """#if defined(BOOST_WINDOWS)

#include <boost/winapi/config.hpp>
#if BOOST_USE_WINAPI_VERSION >= BOOST_WINAPI_VERSION_WIN8
#define BOOST_ATOMIC_DETAIL_WAIT_BACKEND windows
#endif

#else // defined(BOOST_WINDOWS)
"""
if old not in text:
    raise SystemExit("Boost.Atomic Windows wait backend block not found")
platform.write_text(text.replace(old, new, 1))

jamfile = Path("libs/atomic/build/Jamfile.v2")
text = jamfile.read_text()
for line in (
    "      <target-os>windows:<library>synchronization\n",
    "      <toolset>gcc,<target-os>windows:<define>_WIN32_WINNT=0x0A00\n",
):
    if line not in text:
        raise SystemExit(f"Boost.Atomic Jamfile line not found: {line.rstrip()}")
    text = text.replace(line, "", 2)
jamfile.write_text(text)
PY

./bootstrap.sh --prefix="$install_prefix" --with-libraries=log
./b2 --with-log -j"$(nproc)" \
  toolset=gcc \
  variant=release \
  link=static \
  runtime-link=shared \
  threading=multi \
  cxxstd=11 \
  address-model=64 \
  --layout=tagged \
  cflags="${api_floor_flags}" \
  cxxflags="${api_floor_flags}" \
  install
