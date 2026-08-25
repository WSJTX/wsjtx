#!/usr/bin/env bash

set -euo pipefail

# Only files that affect Linux CI image contents or its recipe fingerprint belong here.
is_linux_ci_image_input() {
  case "$1" in
    .github/images/linux-ci/*|.github/scripts/linux-ci-image-config.sh|.github/scripts/linux-ci-image-fingerprint.sh|.github/scripts/run-apt-get.sh|.github/scripts/build-pfunit-linux.sh|.github/scripts/build-hamlib-linux.sh|.github/scripts/tsan-linux-deps-config.sh|.github/scripts/build-qt-tsan-linux.sh|.github/scripts/build-boost-tsan-linux.sh|.github/scripts/build-hamlib-tsan-linux.sh|.github/scripts/verify-linux-ci-image.sh|.github/scripts/verify-tsan-deps-linux.sh)
      return 0
      ;;
    *)
      return 1
      ;;
  esac
}

expect_input() {
  if ! is_linux_ci_image_input "$1"; then
    echo "self-test failed: expected Linux image input: $1" >&2
    exit 1
  fi
}

expect_non_input() {
  if is_linux_ci_image_input "$1"; then
    echo "self-test failed: unexpected Linux image input: $1" >&2
    exit 1
  fi
}

if [ "${1:-}" = "--self-test" ]; then
  expect_input ".github/images/linux-ci/Dockerfile.noble"
  expect_input ".github/scripts/build-hamlib-linux.sh"
  expect_input ".github/scripts/build-qt-tsan-linux.sh"
  expect_input ".github/scripts/verify-linux-ci-image.sh"

  expect_non_input ".github/workflows/build-linux.yml"
  expect_non_input ".github/actions/build-linux-payload/action.yml"
  expect_non_input "CMake/Sources.cmake"
  expect_non_input ".github/scripts/build-boost-macos.sh"
  exit 0
fi

if [ "$#" -ne 1 ]; then
  echo "Usage: is-linux-ci-image-input.sh PATH" >&2
  exit 2
fi

is_linux_ci_image_input "$1"
