#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 4 ]; then
  echo "Usage: write-armhf-ci-manifest.sh ROLE ARCH GENERATION RECIPE_SHA256" >&2
  exit 2
fi

requested_role=$1
requested_arch=$2
requested_generation=$3
requested_recipe_sha256=$4
role=$requested_role
arch=$requested_arch
generation=$requested_generation
recipe_sha256=$requested_recipe_sha256
config_dir=${WSJTX_ARMHF_CONFIG_DIR:-/usr/local/share/wsjtx-ci}
manifest_dir=${WSJTX_CI_MANIFEST_DIR:-/opt/wsjtx-ci}
declare toolchain_id='' runtime_sha256='' hamlib_ref='' hamlib_commit=''

# shellcheck source=.github/scripts/armhf-ci-image-config.sh
. "$config_dir/armhf-ci-image-config.sh"

hash_package_set() {
  dpkg-query -W -f='${binary:Package}=${Version}\n' |
    LC_ALL=C sort |
    sha256sum |
    awk '{print $1}'
}

hash_runtime_set() {
  local library records=""
  for library in "${ARMHF_GCC_RUNTIME_LIBRARIES[@]}"; do
    records+="$library=$(sha256sum "$ARMHF_RUNTIME_PREFIX/$library" | awk '{print $1}')"$'\n'
  done
  printf '%s' "$records" | sha256sum | awk '{print $1}'
}

mkdir -p "$manifest_dir"
case "$role" in
  cross-builder)
    compiler="$ARMHF_TOOLCHAIN_PREFIX/bin/$ARMHF_TARGET_TRIPLET-gcc"
    signature_helper="$config_dir/linux-ccache-compiler-signature.sh"
    IFS=$'\t' read -r compiler_version compiler_target compiler_sha256 \
      ccache_compatibility_id compiler_signature_sha256 \
      < <("$signature_helper" "$compiler")
    sysroot_package_sha256="$(sha256sum "$manifest_dir/armhf-sysroot-packages.txt" | awk '{print $1}')"
    runtime_sha256="$(hash_runtime_set)"
    hamlib_sha256="$(sha256sum "$ARMHF_SYSROOT/usr/lib/libhamlib.a" | awk '{print $1}')"
    package_sha256="$(hash_package_set)"
    identity="$recipe_sha256:$compiler_target:$compiler_sha256:$sysroot_package_sha256:$runtime_sha256:$hamlib_sha256"
    identity_sha256="$(printf '%s' "$identity" | sha256sum | awk '{print $1}')"
    toolchain_id="gcc${compiler_version}-${arch}-${identity_sha256:0:20}"
    {
      printf 'schema=%s\n' "$ARMHF_CI_IMAGE_SCHEMA"
      printf 'role=%s\n' "$role"
      printf 'architecture=%s\n' "$arch"
      printf 'generation=%s\n' "$generation"
      printf 'recipe_sha256=%s\n' "$recipe_sha256"
      printf 'compiler_version=%s\n' "$compiler_version"
      printf 'compiler_target=%s\n' "$compiler_target"
      printf 'compiler_sha256=%s\n' "$compiler_sha256"
      printf 'compiler_signature_sha256=%s\n' "$compiler_signature_sha256"
      printf 'ccache_compatibility_id=%s\n' "$ccache_compatibility_id"
      printf 'toolchain_id=%s\n' "$toolchain_id"
      printf 'host_dpkg_arch=%s\n' "$(dpkg --print-architecture)"
      printf 'package_sha256=%s\n' "$package_sha256"
      printf 'sysroot_package_sha256=%s\n' "$sysroot_package_sha256"
      printf 'runtime_sha256=%s\n' "$runtime_sha256"
      printf 'hamlib_ref=%s\n' "$LINUX_HAMLIB_REF"
      printf 'hamlib_commit=%s\n' "$LINUX_HAMLIB_COMMIT"
      printf 'hamlib_sha256=%s\n' "$hamlib_sha256"
      printf 'glibc_version=%s\n' "$ARMHF_GLIBC_VERSION"
      printf 'binutils_version=%s\n' "$ARMHF_BINUTILS_VERSION"
      printf 'linux_headers_version=%s\n' "$ARMHF_LINUX_HEADERS_VERSION"
      printf 'minimum_kernel_version=%s\n' "$ARMHF_MIN_KERNEL_VERSION"
      printf 'quadmath_runtime=%s\n' unavailable-on-arm
    } > "$manifest_dir/image.env"
    ;;
  runtime)
    cross_manifest="$config_dir/cross-builder.env"
    if [ ! -f "$cross_manifest" ]; then
      echo "Cross-builder manifest is missing: $cross_manifest" >&2
      exit 1
    fi
    # shellcheck disable=SC1090
    . "$cross_manifest"
    cross_generation=$generation
    cross_recipe_sha256=$recipe_sha256
    cross_toolchain_id=$toolchain_id
    cross_runtime_sha256=$runtime_sha256
    cross_hamlib_ref=$hamlib_ref
    cross_hamlib_commit=$hamlib_commit
    runtime_package_sha256="$(hash_package_set)"
    runtime_sha256="$(hash_runtime_set)"
    if [ "$runtime_sha256" != "$cross_runtime_sha256" ]; then
      echo "Runtime GCC library set differs from the cross-builder" >&2
      exit 1
    fi
    {
      printf 'schema=%s\n' "$ARMHF_CI_IMAGE_SCHEMA"
      printf 'role=%s\n' "$requested_role"
      printf 'architecture=%s\n' "$requested_arch"
      printf 'generation=%s\n' "$requested_generation"
      printf 'recipe_sha256=%s\n' "$requested_recipe_sha256"
      printf 'cross_generation=%s\n' "$cross_generation"
      printf 'cross_recipe_sha256=%s\n' "$cross_recipe_sha256"
      printf 'cross_toolchain_id=%s\n' "$cross_toolchain_id"
      printf 'runtime_sha256=%s\n' "$runtime_sha256"
      printf 'package_sha256=%s\n' "$runtime_package_sha256"
      printf 'hamlib_ref=%s\n' "$cross_hamlib_ref"
      printf 'hamlib_commit=%s\n' "$cross_hamlib_commit"
      printf 'glibc_version=%s\n' "$ARMHF_GLIBC_VERSION"
      printf 'quadmath_runtime=%s\n' unavailable-on-arm
    } > "$manifest_dir/image.env"
    ;;
  *)
    echo "Unsupported ARMHF image role: $role" >&2
    exit 2
    ;;
esac
