#!/usr/bin/env bash
set -euo pipefail

VERSION="${VERSION:?missing}"
ARCH="${ARCH:?missing}"
HAMLIB_BRANCH="${HAMLIB_BRANCH:?missing}"
phase=${ARMHF_RUNTIME_PHASE:-all}
if [ "$ARCH" != armhf ]; then
  echo "validate-linux-armhf-runtime.sh supports only arch=armhf" >&2
  exit 2
fi
case "$phase" in
  all|test|package) ;;
  *) echo "Unsupported ARMHF runtime phase: $phase" >&2; exit 2 ;;
esac

github_api_token="${GITHUB_TOKEN:-}"
unset GITHUB_TOKEN
cd /work
# shellcheck source=.github/scripts/armhf-ci-image-config.sh
source .github/scripts/armhf-ci-image-config.sh
.github/scripts/verify-armhf-ci-image.sh runtime "$ARCH" "$HAMLIB_BRANCH"

if [ "$phase" = all ] || [ "$phase" = test ]; then
  echo "::group::ARMHF CTest under QEMU"
  started="$(date +%s)"
  set +e
  (
    cd wsjtx-build
    LD_LIBRARY_PATH="$ARMHF_RUNTIME_PREFIX" \
      QT_QPA_PLATFORM=xcb \
      xvfb-run -a -s "-screen 0 1280x1024x24" \
      ctest --output-on-failure --output-junit ctest-results.xml
  ) 2>&1 | tee wsjtx-build/ctest-armhf.log
  status=${PIPESTATUS[0]}
  set -e
  ended="$(date +%s)"
  {
    printf 'started_epoch=%s\n' "$started"
    printf 'ended_epoch=%s\n' "$ended"
    printf 'duration_seconds=%s\n' "$((ended - started))"
    printf 'status=%s\n' "$status"
  } > wsjtx-build/armhf-ctest-timing.env
  echo "::endgroup::"
  if [ "$status" -ne 0 ]; then
    exit "$status"
  fi
fi

if [ "$phase" = all ] || [ "$phase" = package ]; then
  started="$(date +%s)"
  set +e
  GITHUB_TOKEN="$github_api_token" .github/scripts/package-linux-armhf.sh
  status=$?
  set -e
  ended="$(date +%s)"
  {
    printf 'started_epoch=%s\n' "$started"
    printf 'ended_epoch=%s\n' "$ended"
    printf 'duration_seconds=%s\n' "$((ended - started))"
    printf 'status=%s\n' "$status"
  } > wsjtx-build/armhf-package-timing.env
  if [ "$status" -ne 0 ]; then
    exit "$status"
  fi
fi
