#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 2 ]; then
  echo "Usage: $0 ARTIFACTS_DIRECTORY VERSION" >&2
  exit 2
fi

artifacts_directory="$1"
version="$2"

find "$artifacts_directory" -maxdepth 2 -type f \
  \( -path "$artifacts_directory/wsjtx-${version}-arm64-macOS.pkg/*.pkg" \
     -o -path "$artifacts_directory/wsjtx-${version}-x86_64-macOS.pkg/*.pkg" \
     -o -path "$artifacts_directory/wsjtx-${version}-linux-*-AppImage/*.AppImage" \
     -o -path "$artifacts_directory/wsjtx-${version}-linux-*-deb/*.deb" \
     -o -path "$artifacts_directory/wsjtx-${version}-linux-*-rpm/*.rpm" \
     -o -path "$artifacts_directory/wsjtx-${version}-windows-x86_64-installer/*.exe" \
     -o -path "$artifacts_directory/wsjtx-${version}-src.tar.gz" \
  \) -print | sort
