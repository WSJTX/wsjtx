#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 3 ]; then
  echo "Usage: build-portaudio-macos.sh VERSION DEPLOYMENT_TARGET PREFIX" >&2
  exit 2
fi

version="$1"
deployment_target="$2"
prefix="$3"

case "$version" in
  19.7.0)
    archive_name="pa_stable_v190700_20210406"
    source_url="https://files.portaudio.com/archives/${archive_name}.tgz"
    source_dir="portaudio"
    ;;
  *)
    echo "Unsupported PortAudio version: $version" >&2
    exit 2
    ;;
esac

curl -L --fail --retry 5 --retry-delay 10 -o portaudio.tgz "$source_url"
tar -xzf portaudio.tgz
cd "$source_dir"

# PortAudio 19.7.0's generated configure script overwrites caller CFLAGS on
# macOS, hardcodes a host-derived deployment target, and enables -Werror.
# Patch the generated script so CI controls the deployment target and modern
# Apple Clang warnings do not break a pinned third-party runtime build.
sed \
  -e 's/ -Wno-deprecated -Werror/ -Wno-deprecated/g' \
  -e "s/mac_version_min=\"-mmacosx-version-min=[0-9.]*\"/mac_version_min=\"-mmacosx-version-min=${deployment_target}\"/g" \
  configure > configure.tmp
mv configure.tmp configure
chmod +x configure

./configure \
  --prefix="$prefix" \
  --enable-shared --disable-static
make -j"$(sysctl -n hw.ncpu)"
make install
