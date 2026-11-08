#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 2 ]; then
  echo "Usage: build-boost-linux.sh VERSION PREFIX" >&2
  exit 2
fi

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
# shellcheck source=.github/scripts/linux-boost-config.sh
. "$script_dir/linux-boost-config.sh"
linux_boost_source_config "$1"
prefix=$2
if [ -e "$prefix" ] && [ -n "$(ls -A "$prefix")" ]; then
  echo "Boost prefix already exists and is not empty: $prefix" >&2
  exit 1
fi
mkdir -p "$prefix/share/wsjtx"
prefix="$(cd "$prefix" && pwd)"
compiler="$(command -v "${CXX:-g++}")"
triplet="$($compiler -dumpmachine)"
host_arch="$(dpkg-architecture -t "$triplet" -qDEB_HOST_ARCH)"
# The build images match the target distribution; select its target-architecture
# compile hardening without applying executable PIE link flags to static archives.
export DEB_HOST_ARCH="$host_arch" DEB_HOST_GNU_TYPE="$triplet"
export DEB_BUILD_MAINT_OPTIONS="${DEB_BUILD_MAINT_OPTIONS:-} hardening=+all"
distro_cppflags="$(dpkg-buildflags --get CPPFLAGS)"
distro_cxxflags="$(dpkg-buildflags --get CXXFLAGS)"
cxxflags="$distro_cppflags $distro_cxxflags ${CPPFLAGS:-} ${CXXFLAGS:-} -fPIC"
linkflags="${LDFLAGS:-}"
target_options=()
if [ -n "${BOOST_SYSROOT:-}" ]; then
  # Debian's headers and start files live below triplet-qualified directories.
  test "$triplet" = arm-linux-gnueabihf
  cxxflags+=" --sysroot=$BOOST_SYSROOT -isystem $BOOST_SYSROOT/usr/include/$triplet -B$BOOST_SYSROOT/usr/lib/$triplet/"
  linkflags+=" --sysroot=$BOOST_SYSROOT -B$BOOST_SYSROOT/usr/lib/$triplet/ -L$BOOST_SYSROOT/usr/lib/$triplet -Wl,-rpath-link,$BOOST_SYSROOT/lib/$triplet -Wl,-rpath-link,$BOOST_SYSROOT/usr/lib/$triplet"
  target_options=(target-os=linux architecture=arm address-model=32 abi=aapcs binary-format=elf)
fi

build_root="$(mktemp -d "${RUNNER_TEMP:-/tmp}/wsjtx-boost.XXXXXX")"
trap 'rm -rf "$build_root"' EXIT
archive="$build_root/boost.tar.bz2"
curl -L --fail --retry 5 --retry-delay 10 -o "$archive" "$LINUX_BOOST_SOURCE_URL"
printf '%s  %s\n' "$LINUX_BOOST_SOURCE_SHA256" "$archive" | sha256sum -c -
tar -xjf "$archive" -C "$build_root"
cd "$build_root/boost_${LINUX_BOOST_VERSION//./_}"
patch --batch --forward --fuzz=0 -p1 < "$script_dir/boost-log-queue.patch"

printf 'using gcc : wsjtx : "%s" ;\n' "$compiler" > user-config.jam
# Bootstrap builds the build engine for the host, including during cross builds.
CC=gcc CXX=g++ ./bootstrap.sh --prefix="$prefix" --with-toolset=gcc --with-libraries=log
link_options=()
if [ -n "$linkflags" ]; then
  link_options+=("linkflags=$linkflags")
fi
./b2 -j"${CMAKE_BUILD_PARALLEL_LEVEL:-$(nproc)}" \
  --user-config="$PWD/user-config.jam" \
  toolset=gcc-wsjtx variant=release link=static runtime-link=shared threading=multi \
  cxxstd=11 cxxflags="$cxxflags" "${link_options[@]}" \
  "${target_options[@]}" --layout=system install

python3 - "$prefix" "$LINUX_BOOST_VERSION" "$LINUX_BOOST_SOURCE_URL" \
  "$LINUX_BOOST_SOURCE_SHA256" "$compiler" "$cxxflags" "$linkflags" \
  "$(linux_boost_recipe_hash)" <<'PY'
import hashlib
import json
from pathlib import Path
import subprocess
import sys

prefix, version, url, source_hash, compiler, cxxflags, linkflags, recipe_hash = sys.argv[1:]
root = Path(prefix)
archives = sorted((root / 'lib').glob('libboost_*.a'))
if not archives:
    raise SystemExit('Boost build installed no static archives')
for archive in archives:
    with archive.open('rb') as stream:
        if stream.read(8) != b'!<arch>\n':
            raise SystemExit(f'Invalid Boost static archive: {archive}')
if any((root / 'lib').glob('libboost_*.so*')):
    raise SystemExit('Boost build installed unexpected shared libraries')
manifest = {
    'version': version, 'source_url': url, 'source_sha256': source_hash,
    'compiler': {'path': compiler,
                 'version': subprocess.check_output([compiler, '-dumpfullversion', '-dumpversion'], text=True).strip(),
                 'target': subprocess.check_output([compiler, '-dumpmachine'], text=True).strip()},
    'cxxflags': cxxflags, 'linkflags': linkflags, 'link': 'static',
    'runtime_link': 'shared', 'threading': 'multi', 'recipe_sha256': recipe_hash,
    'archives': {path.name: hashlib.sha256(path.read_bytes()).hexdigest() for path in archives},
    'version_header_sha256': hashlib.sha256((root / 'include/boost/version.hpp').read_bytes()).hexdigest(),
}
(root / 'share/wsjtx/boost-build.json').write_text(json.dumps(manifest, indent=2) + '\n')
print(json.dumps(manifest, indent=2))
PY
