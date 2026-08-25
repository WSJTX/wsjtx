#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 1 ]; then
  echo "Usage: linux-ci-image-fingerprint.sh normal-noble|normal-bookworm|tsan-noble" >&2
  exit 2
fi

profile=$1
# Keep this list aligned with files copied or consumed by the image Dockerfiles.
common=(
  .github/images/linux-ci/install-packages.sh
  .github/images/linux-ci/write-manifest.sh
  .github/scripts/linux-ci-image-config.sh
  .github/scripts/run-apt-get.sh
  .github/scripts/verify-linux-ci-image.sh
)

case "$profile" in
  normal-noble)
    files=(
      "${common[@]}"
      .github/images/linux-ci/Dockerfile.noble
      .github/scripts/build-pfunit-linux.sh
      .github/scripts/build-hamlib-linux.sh
    )
    ;;
  normal-bookworm)
    files=(
      "${common[@]}"
      .github/images/linux-ci/Dockerfile.bookworm
      .github/scripts/build-pfunit-linux.sh
      .github/scripts/build-hamlib-linux.sh
    )
    ;;
  tsan-noble)
    files=(
      "${common[@]}"
      .github/images/linux-ci/Dockerfile.noble
      .github/scripts/tsan-linux-deps-config.sh
      .github/scripts/build-qt-tsan-linux.sh
      .github/scripts/build-boost-tsan-linux.sh
      .github/scripts/build-hamlib-tsan-linux.sh
      .github/scripts/verify-tsan-deps-linux.sh
    )
    ;;
  *)
    echo "Unsupported Linux CI image profile: $profile" >&2
    exit 2
    ;;
esac

sha256sum "${files[@]}" | sha256sum | awk '{print $1}'
