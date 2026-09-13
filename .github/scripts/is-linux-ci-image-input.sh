#!/usr/bin/env bash

set -euo pipefail

. .github/scripts/linux-ci-image-inputs.sh

is_linux_ci_image_input() {
  local profile=$1
  local path=$2
  local input inputs

  if ! inputs="$(linux_ci_image_inputs "$profile")"; then
    return 2
  fi

  while IFS= read -r input; do
    if [ "$path" = "$input" ]; then
      return 0
    fi
  done <<< "$inputs"
  return 1
}

linux_ci_image_set_for_inputs() {
  local normal=false tsan=false path

  for path in "$@"; do
    if is_linux_ci_image_input normal-noble "$path" ||
       is_linux_ci_image_input normal-bookworm "$path" ||
       is_linux_ci_image_input armhf-cross-bookworm "$path" ||
       is_linux_ci_image_input armhf-runtime-bookworm "$path"; then
      normal=true
    fi
    if is_linux_ci_image_input tsan-noble "$path"; then
      tsan=true
    fi
  done

  if [ "$normal" = true ] && [ "$tsan" = true ]; then
    echo normal-and-tsan
  elif [ "$normal" = true ]; then
    echo normal
  elif [ "$tsan" = true ]; then
    echo tsan
  else
    echo none
  fi
}

expect_input() {
  if ! is_linux_ci_image_input "$1" "$2"; then
    echo "self-test failed: expected $1 image input: $2" >&2
    exit 1
  fi
}

expect_non_input() {
  if is_linux_ci_image_input "$1" "$2"; then
    echo "self-test failed: unexpected $1 image input: $2" >&2
    exit 1
  fi
}

expect_image_set() {
  local expected=$1
  shift
  local actual
  actual="$(linux_ci_image_set_for_inputs "$@")"
  if [ "$actual" != "$expected" ]; then
    echo "self-test failed: expected image set $expected, got $actual" >&2
    exit 1
  fi
}

if [ "${1:-}" = "--self-test" ]; then
  expect_input normal-noble ".github/images/linux-ci/Dockerfile.noble"
  expect_input tsan-noble ".github/images/linux-ci/Dockerfile.noble"
  expect_input normal-bookworm ".github/images/linux-ci/Dockerfile.bookworm"
  expect_input armhf-cross-bookworm ".github/images/linux-ci/Dockerfile.armhf-cross"
  expect_input armhf-runtime-bookworm ".github/images/linux-ci/Dockerfile.armhf-runtime"
  expect_input armhf-runtime-bookworm ".github/images/linux-ci/install-armhf-runtime-packages.sh"
  expect_non_input normal-noble ".github/images/linux-ci/install-armhf-runtime-packages.sh"
  expect_input armhf-cross-bookworm ".github/images/linux-ci/armhf-cross-toolchain.config"
  expect_non_input armhf-runtime-bookworm ".github/images/linux-ci/armhf-cross-toolchain.config"
  expect_input normal-noble ".github/scripts/build-hamlib-linux.sh"
  expect_non_input tsan-noble ".github/scripts/build-hamlib-linux.sh"
  expect_input tsan-noble ".github/scripts/build-qt-tsan-linux.sh"
  expect_non_input normal-noble ".github/scripts/build-qt-tsan-linux.sh"
  expect_input normal-noble ".github/scripts/verify-linux-ci-image.sh"
  expect_input tsan-noble ".github/scripts/verify-linux-ci-image.sh"

  expect_non_input normal-noble ".github/workflows/build-linux.yml"
  expect_non_input tsan-noble "CMake/Sources.cmake"
  expect_non_input normal-bookworm ".github/scripts/build-boost-macos.sh"
  expect_image_set normal ".github/scripts/build-hamlib-linux.sh"
  expect_image_set tsan ".github/scripts/build-qt-tsan-linux.sh"
  expect_image_set normal-and-tsan ".github/scripts/verify-linux-ci-image.sh"
  expect_image_set normal-and-tsan \
    ".github/scripts/build-hamlib-linux.sh" \
    ".github/scripts/build-qt-tsan-linux.sh"
  expect_image_set none "CMake/Sources.cmake"
  if is_linux_ci_image_input unsupported ".github/images/linux-ci/Dockerfile.noble" 2>/dev/null; then
    echo "self-test failed: unsupported image profile was accepted" >&2
    exit 1
  elif [ "$?" -ne 2 ]; then
    echo "self-test failed: unsupported image profile did not return status 2" >&2
    exit 1
  fi
  exit 0
fi

if [ "${1:-}" = "--classify" ]; then
  shift
  if [ "$#" -eq 0 ]; then
    echo "Usage: is-linux-ci-image-input.sh --classify PATH [PATH ...]" >&2
    exit 2
  fi
  linux_ci_image_set_for_inputs "$@"
  exit 0
fi

if [ "$#" -ne 2 ]; then
  echo "Usage: is-linux-ci-image-input.sh PROFILE PATH | --classify PATH [PATH ...]" >&2
  exit 2
fi

is_linux_ci_image_input "$1" "$2"
