#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 3 ]; then
  echo "Usage: build-boost-windows.sh VERSION WIN32_WINNT PREFIX" >&2
  exit 2
fi

version="$1"
win32_winnt="$2"
prefix="$3"
version_underscores=${version//./_}
api_floor_flags="-D_WIN32_WINNT=${win32_winnt} -DWINVER=${win32_winnt} -DBOOST_USE_WINAPI_VERSION=${win32_winnt}"
install_prefix=$(cygpath -m "$prefix")

curl -L --fail --retry 5 --retry-delay 10 -o boost.tar.bz2 "https://archives.boost.io/release/${version}/source/boost_${version_underscores}.tar.bz2"
tar -xjf boost.tar.bz2
cd "boost_${version_underscores}"

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
  link=shared \
  runtime-link=shared \
  threading=multi \
  cxxstd=11 \
  address-model=64 \
  --layout=tagged \
  cflags="${api_floor_flags}" \
  cxxflags="${api_floor_flags}" \
  install
