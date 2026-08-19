#!/usr/bin/env bash
set -euo pipefail

fixture="$(mktemp -d)"
trap 'rm -rf "$fixture"' EXIT
mkdir -p "$fixture/prefix/pfunit/cmake" "$fixture/prefix/hamlib/lib"
touch "$fixture/prefix/pfunit/cmake/PFUNITConfig.cmake"
touch "$fixture/prefix/hamlib/lib/libhamlib.a"

# shellcheck source=.github/scripts/linux-ci-image-config.sh
. .github/scripts/linux-ci-image-config.sh
recipe="$(.github/scripts/linux-ci-image-fingerprint.sh normal-noble)"
manifest="$fixture/image.env"

write_manifest() {
  local hamlib_commit=${1:-$LINUX_HAMLIB_COMMIT}
  local recipe_sha=${2:-$recipe}
  {
    echo "schema=1"
    echo "flavor=normal"
    echo "architecture=x86_64"
    echo "generation=build-20260818-1-1"
    echo "compiler_target=x86_64-linux-gnu"
    echo "package_sha256=test"
    echo "toolchain_id=test"
    echo "hamlib_ref=$LINUX_HAMLIB_REF"
    echo "hamlib_commit=$hamlib_commit"
    echo "pfunit_version=$LINUX_PFUNIT_VERSION"
    echo "pfunit_commit=$LINUX_PFUNIT_COMMIT"
    echo "recipe_sha256=$recipe_sha"
  } > "$manifest"
}

run_verify() {
  WSJTX_CI_IMAGE_MANIFEST="$manifest" \
    WSJTX_CI_PREFIX_OVERRIDE="$fixture/prefix" \
    .github/scripts/verify-linux-ci-image.sh normal x86_64 "$LINUX_HAMLIB_REF"
}

write_manifest
run_verify >/dev/null

write_manifest 0000000000000000000000000000000000000000
if run_verify >/dev/null 2>&1; then
  echo "Expected a mismatched Hamlib commit to fail" >&2
  exit 1
fi

write_manifest "$LINUX_HAMLIB_COMMIT" 0000000000000000000000000000000000000000000000000000000000000000
if run_verify >/dev/null 2>&1; then
  echo "Expected a mismatched recipe fingerprint to fail" >&2
  exit 1
fi

echo "verify-linux-ci-image.sh self-test passed"
