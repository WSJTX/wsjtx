#!/usr/bin/env bash
set -euo pipefail

fixture="$(mktemp -d)"
trap 'rm -rf "$fixture"' EXIT
mkdir -p "$fixture/prefix/pfunit/cmake" "$fixture/prefix/hamlib/lib"
touch "$fixture/prefix/pfunit/cmake/PFUNITConfig.cmake"
touch "$fixture/prefix/hamlib/lib/libhamlib.a"

# shellcheck source=.github/scripts/linux-ci-image-config.sh
. .github/scripts/linux-ci-image-config.sh
# shellcheck source=.github/scripts/tsan-linux-deps-config.sh
. .github/scripts/tsan-linux-deps-config.sh
recipe="$(.github/scripts/linux-ci-image-fingerprint.sh normal-noble)"
tsan_recipe="$(.github/scripts/linux-ci-image-fingerprint.sh tsan-noble)"
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
  local allow_stale_image=${1:-false}
  WSJTX_CI_IMAGE_MANIFEST="$manifest" \
    WSJTX_CI_PREFIX_OVERRIDE="$fixture/prefix" \
    WSJTX_CI_IMAGE_SKIP_RUNTIME_CHECKS=true \
    WSJTX_CI_IMAGE_ALLOW_RECIPE_MISMATCH="$allow_stale_image" \
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
if ! run_verify true >/dev/null 2>&1; then
  echo "Expected stale image allowance to accept a mismatched recipe fingerprint" >&2
  exit 1
fi

tsan_config_dir="$fixture/tsan-config"
mkdir -p "$tsan_config_dir" "$fixture/prefix/tsan/boost/cmake"
cp .github/scripts/verify-linux-ci-image.sh \
  .github/scripts/linux-ci-image-config.sh \
  .github/scripts/tsan-linux-deps-config.sh \
  "$tsan_config_dir/"
cp .github/scripts/linux-ci-image-fingerprint.sh "$tsan_config_dir/"
cat > "$tsan_config_dir/verify-tsan-deps-linux.sh" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
EOF
chmod +x "$tsan_config_dir/verify-tsan-deps-linux.sh"
touch "$fixture/prefix/tsan/boost/cmake/BoostConfig.cmake"

tsan_manifest="$fixture/tsan-image.env"
cat > "$tsan_manifest" <<EOF
schema=1
flavor=tsan
architecture=x86_64
generation=build-20260818-1-1
compiler=gcc-13
compiler_version=$TSAN_GCC_VERSION
compiler_sha256=fixture
compiler_target=fixture
package_sha256=fixture
toolchain_id=fixture
hamlib_ref=$TSAN_HAMLIB_REF
hamlib_commit=$TSAN_HAMLIB_COMMIT
hamlib_patch_commit=$TSAN_HAMLIB_PATCH_COMMIT
qt_version=$TSAN_QT_VERSION
boost_version=$TSAN_BOOST_VERSION
recipe_sha256=$tsan_recipe
EOF
if ! WSJTX_CI_IMAGE_MANIFEST="$tsan_manifest" \
  WSJTX_CI_PREFIX_OVERRIDE="$fixture/prefix" \
  WSJTX_CI_IMAGE_SKIP_RUNTIME_CHECKS=true \
  IMAGE_RECIPE_SHA256="$tsan_recipe" \
  "$tsan_config_dir/verify-linux-ci-image.sh" \
    tsan x86_64 "$TSAN_HAMLIB_REF" >/dev/null; then
  echo "Expected TSan image verification to load its dependency configuration" >&2
  exit 1
fi

echo "verify-linux-ci-image.sh self-test passed"
