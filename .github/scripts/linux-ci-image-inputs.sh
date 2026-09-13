#!/usr/bin/env bash

# Prints the files whose contents define a Linux CI image profile.
linux_ci_image_inputs() {
  local profile=$1
  local common=(
    .github/images/linux-ci/install-packages.sh
    .github/images/linux-ci/write-manifest.sh
    .github/scripts/linux-ci-image-fingerprint.sh
    .github/scripts/linux-ci-image-inputs.sh
    .github/scripts/linux-ci-image-config.sh
    .github/scripts/linux-ccache-compiler-signature.sh
    .github/scripts/run-apt-get.sh
    .github/scripts/verify-linux-ci-image.sh
  )

  case "$profile" in
    normal-noble)
      printf '%s\n' \
        "${common[@]}" \
        .github/images/linux-ci/Dockerfile.noble \
        .github/scripts/build-pfunit-linux.sh \
        .github/scripts/build-hamlib-linux.sh
      ;;
    normal-bookworm)
      printf '%s\n' \
        "${common[@]}" \
        .github/images/linux-ci/Dockerfile.bookworm \
        .github/scripts/build-pfunit-linux.sh \
        .github/scripts/build-hamlib-linux.sh
      ;;
    armhf-cross-bookworm)
      printf '%s\n' \
        .github/images/linux-ci/Dockerfile.armhf-cross \
        .github/images/linux-ci/armhf-cross-toolchain.config \
        .github/scripts/armhf-ci-image-config.sh \
        .github/scripts/build-armhf-toolchain-smoke.sh \
        .github/scripts/build-hamlib-armhf-cross.sh \
        .github/scripts/linux-ccache-compiler-signature.sh \
        .github/scripts/linux-ci-image-config.sh \
        .github/scripts/linux-ci-image-fingerprint.sh \
        .github/scripts/linux-ci-image-inputs.sh \
        .github/scripts/normalize-armhf-sysroot-symlinks.sh \
        .github/scripts/run-apt-get.sh \
        .github/scripts/verify-armhf-ci-image.sh \
        .github/scripts/write-armhf-ci-manifest.sh
      ;;
    armhf-runtime-bookworm)
      printf '%s\n' \
        .github/images/linux-ci/Dockerfile.armhf-runtime \
        .github/images/linux-ci/install-armhf-runtime-packages.sh \
        .github/scripts/armhf-ci-image-config.sh \
        .github/scripts/linux-ci-image-config.sh \
        .github/scripts/linux-ci-image-fingerprint.sh \
        .github/scripts/linux-ci-image-inputs.sh \
        .github/scripts/run-apt-get.sh \
        .github/scripts/verify-armhf-ci-image.sh \
        .github/scripts/write-armhf-ci-manifest.sh
      ;;
    tsan-noble)
      printf '%s\n' \
        "${common[@]}" \
        .github/images/linux-ci/Dockerfile.noble \
        .github/scripts/tsan-linux-deps-config.sh \
        .github/scripts/build-qt-tsan-linux.sh \
        .github/scripts/build-boost-tsan-linux.sh \
        .github/scripts/build-hamlib-tsan-linux.sh \
        .github/scripts/verify-tsan-deps-linux.sh
      ;;
    *)
      echo "Unsupported Linux CI image profile: $profile" >&2
      return 2
      ;;
  esac
}
