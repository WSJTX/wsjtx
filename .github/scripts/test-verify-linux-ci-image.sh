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
compiler=gcc
compiler_path="$(readlink -f "$(command -v "$compiler")")"
compiler_version="$($compiler -dumpfullversion -dumpversion)"
compiler_sha256="$(sha256sum "$compiler_path" | awk '{print $1}')"
compiler_target="$($compiler -dumpmachine)"
if command -v dpkg-query >/dev/null 2>&1; then
  package_sha256="$(dpkg-query -W -f='${binary:Package}=${Version}\n' | LC_ALL=C sort | sha256sum | awk '{print $1}')"
  toolchain_identity="$recipe:$compiler_target:$compiler_sha256:$package_sha256"
  toolchain_sha256="$(printf '%s' "$toolchain_identity" | sha256sum | awk '{print $1}')"
  toolchain_id="gcc${compiler_version}-x86_64-${toolchain_sha256:0:20}"
else
  package_sha256=fixture
  toolchain_id=fixture
fi

write_manifest() {
  local hamlib_commit=${1:-$LINUX_HAMLIB_COMMIT}
  local recipe_sha=${2:-$recipe}
  {
    echo "schema=1"
    echo "flavor=normal"
    echo "architecture=x86_64"
    echo "generation=build-20260818-1-1"
    echo "compiler=$compiler"
    echo "compiler_version=$compiler_version"
    echo "compiler_sha256=$compiler_sha256"
    echo "compiler_target=$compiler_target"
    echo "package_sha256=$package_sha256"
    echo "toolchain_id=$toolchain_id"
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
    WSJTX_CI_IMAGE_SKIP_RUNTIME_CHECKS=true \
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
