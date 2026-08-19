#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 3 ]; then
  echo "Usage: verify-linux-ci-image.sh FLAVOR ARCH HAMLIB_REF" >&2
  exit 2
fi

expected_flavor=$1
expected_arch=$2
expected_hamlib_ref=$3
manifest=${WSJTX_CI_IMAGE_MANIFEST:-/opt/wsjtx-ci/image.env}
image_prefix=${WSJTX_CI_PREFIX_OVERRIDE:-/opt/wsjtx}

# shellcheck source=.github/scripts/linux-ci-image-config.sh
. .github/scripts/linux-ci-image-config.sh

if [ ! -f "$manifest" ]; then
  echo "Linux CI image manifest not found: $manifest" >&2
  exit 1
fi

# shellcheck disable=SC1090
. "$manifest"

require_equal() {
  local field=$1 expected=$2 actual=$3
  if [ "$actual" != "$expected" ]; then
    echo "Linux CI image $field mismatch: expected $expected, got $actual" >&2
    exit 1
  fi
}

require_equal schema 1 "${schema:-}"
require_equal flavor "$expected_flavor" "${flavor:-}"
require_equal architecture "$expected_arch" "${architecture:-}"
require_equal hamlib_ref "$expected_hamlib_ref" "${hamlib_ref:-}"

case "$expected_flavor" in
  normal)
    require_equal hamlib_commit "$LINUX_HAMLIB_COMMIT" "${hamlib_commit:-}"
    require_equal pfunit_version "$LINUX_PFUNIT_VERSION" "${pfunit_version:-}"
    require_equal pfunit_commit "$LINUX_PFUNIT_COMMIT" "${pfunit_commit:-}"
    profile=normal-noble
    if [ "$expected_arch" != x86_64 ]; then
      profile=normal-bookworm
    fi
    pfunit_config="$(find "$image_prefix/pfunit" -name PFUNITConfig.cmake -print -quit)"
    if [ -z "$pfunit_config" ]; then
      echo "PFUNITConfig.cmake not found in the Linux CI image" >&2
      exit 1
    fi
    test -f "$image_prefix/hamlib/lib/libhamlib.a"
    pfunit_dir="$(dirname "$pfunit_config")"
    hamlib_prefix=$image_prefix/hamlib
    ;;
  tsan)
    profile=tsan-noble
    .github/scripts/verify-tsan-deps-linux.sh \
      "$image_prefix/tsan/qt" "$image_prefix/tsan/boost" "$image_prefix/tsan/hamlib"
    ;;
  *)
    echo "Unsupported Linux CI image flavor: $expected_flavor" >&2
    exit 2
    ;;
esac

expected_recipe="$(.github/scripts/linux-ci-image-fingerprint.sh "$profile")"
require_equal recipe_sha256 "$expected_recipe" "${recipe_sha256:-}"
test -n "${compiler_target:-}"
test -n "${package_sha256:-}"
test -n "${toolchain_id:-}"

echo "Linux CI image: ${generation:-unknown} (${toolchain_id:-unknown})"

if [ -n "${GITHUB_OUTPUT:-}" ]; then
  {
    printf 'generation=%s\n' "$generation"
    printf 'toolchain_id=%s\n' "$toolchain_id"
    if [ "$expected_flavor" = normal ]; then
      printf 'pfunit_dir=%s\n' "$pfunit_dir"
      printf 'hamlib_prefix=%s\n' "$hamlib_prefix"
    else
      printf 'qt_prefix=%s\n' "$image_prefix/tsan/qt"
      printf 'boost_prefix=%s\n' "$image_prefix/tsan/boost"
      printf 'hamlib_prefix=%s\n' "$image_prefix/tsan/hamlib"
    fi
  } >> "$GITHUB_OUTPUT"
fi
