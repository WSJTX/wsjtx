#!/usr/bin/env bash
# Decide whether a Linux CI build should create and upload packages.

set -euo pipefail

path_triggers_packaging() {
  case "$1" in
    CMakeLists.txt|*/CMakeLists.txt|CMake/*|cmake/*|CMakeCPackOptions.cmake.in|package_description.txt|\
    debian/*|bundle_fixup/*|artwork/*|icons/*|sounds/*|data/*|Palettes/*|translations/*|\
    example_log_configurations/*|contrib/Ephemeris/*|contrib/gpl-v3-logo.svg|*.desktop|\
    ALLCALL7.TXT|CALL3.TXT|cty.dat|cty.dat_copyright.txt|grid.dat|sat.dat|eclipse.txt|\
    COPYING|AUTHORS|THANKS|NEWS|BUGS|README.md|\
    .github/actions/build-linux-payload/*|\
    .github/workflows/ci.yml|.github/workflows/build-linux.yml|.github/workflows/build-selected-artifacts.yml|.github/workflows/release.yml|\
    .github/scripts/select-linux-packaging.sh|.github/scripts/linux-artifact-validation.sh|.github/scripts/package-linux-armhf.sh|\
    .github/scripts/validate-linux-armhf-runtime.sh|.github/scripts/build-linux-armhf-cross.sh|\
    .github/cmake/*|.github/images/linux-ci/*)
      return 0
      ;;
    *)
      return 1
      ;;
  esac
}

expect_trigger() {
  if ! path_triggers_packaging "$1"; then
    echo "self-test failed: expected packaging for $1" >&2
    exit 1
  fi
}

expect_no_trigger() {
  if path_triggers_packaging "$1"; then
    echo "self-test failed: did not expect packaging for $1" >&2
    exit 1
  fi
}

if [ "${1:-}" = "--self-test" ]; then
  expect_trigger "CMakeLists.txt"
  expect_trigger "tests/unit/audio/CMakeLists.txt"
  expect_trigger "CMake/Install.cmake"
  expect_trigger "debian/postinst"
  expect_trigger "sounds/73.wav"
  expect_trigger "cty.dat"
  expect_trigger "contrib/Ephemeris/JPLEPH"
  expect_trigger "wsjtx.desktop"
  expect_trigger ".github/actions/build-linux-payload/action.yml"
  expect_trigger ".github/workflows/build-linux.yml"
  expect_trigger ".github/scripts/select-linux-packaging.sh"
  expect_trigger ".github/scripts/linux-artifact-validation.sh"
  expect_trigger ".github/images/linux-ci/install-packages.sh"

  expect_no_trigger "widgets/mainwindow.cpp"
  expect_no_trigger "tests/unit/audio/test_tx_request.cpp"
  expect_no_trigger ".github/workflows/build-macos.yml"
  expect_no_trigger "doc/user_guide/en/wsjtx-main.adoc"

  echo "select-linux-packaging.sh self-test passed"
  exit 0
fi

build_packages=true
reason="${EVENT_NAME:-unknown} event"

if [ "${EVENT_NAME:-}" = "pull_request" ] && [ "${FULL_CI:-false}" != "true" ]; then
  base="${PR_BASE_SHA:-}"
  head="${CURRENT_SHA:-}"
  build_packages=false
  reason="ordinary pull request"

  if ! git cat-file -e "${base}^{commit}" 2>/dev/null; then
    git fetch --no-tags --depth=1 origin "$base" || true
  fi

  if git cat-file -e "${base}^{commit}" 2>/dev/null &&
      git cat-file -e "${head}^{commit}" 2>/dev/null; then
    while IFS= read -r path; do
      [ -z "$path" ] && continue
      if path_triggers_packaging "$path"; then
        build_packages=true
        reason="$path"
        break
      fi
    done < <(git diff --name-only "$base" "$head")
  else
    build_packages=true
    reason="unreadable ${base}..${head}"
  fi
fi

echo "Build Linux packages: $build_packages"
echo "Packaging policy reason: $reason"
if [ -n "${GITHUB_OUTPUT:-}" ]; then
  echo "build_packages=$build_packages" >> "$GITHUB_OUTPUT"
fi
