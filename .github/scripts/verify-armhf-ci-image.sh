#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 3 ]; then
  echo "Usage: verify-armhf-ci-image.sh ROLE ARCH HAMLIB_REF" >&2
  exit 2
fi

expected_role=$1
expected_arch=$2
expected_hamlib_ref=$3
manifest=${WSJTX_CI_IMAGE_MANIFEST:-/opt/wsjtx-ci/image.env}
config_dir=/usr/local/share/wsjtx-ci
if [ ! -f "$config_dir/armhf-ci-image-config.sh" ]; then
  config_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
fi
# shellcheck source=.github/scripts/armhf-ci-image-config.sh
. "$config_dir/armhf-ci-image-config.sh"

if [ ! -f "$manifest" ]; then
  echo "ARMHF CI image manifest not found: $manifest" >&2
  exit 1
fi
declare schema='' role='' architecture='' generation='' recipe_sha256=''
declare compiler_version='' compiler_target='' compiler_sha256=''
declare compiler_signature_sha256='' ccache_compatibility_id='' toolchain_id=''
declare host_dpkg_arch='' package_sha256='' sysroot_package_sha256='' runtime_sha256=''
declare hamlib_ref='' hamlib_commit='' hamlib_sha256='' glibc_version=''
declare binutils_version='' linux_headers_version='' minimum_kernel_version=''
declare quadmath_runtime='' cross_generation='' cross_recipe_sha256='' cross_toolchain_id=''
# shellcheck disable=SC1090
. "$manifest"

require_equal() {
  local field=$1 expected=$2 actual=$3
  if [ "$actual" != "$expected" ]; then
    echo "ARMHF CI image $field mismatch: expected $expected, got $actual" >&2
    exit 1
  fi
}

require_sha256() {
  local field=$1 value=$2
  if [[ ! "$value" =~ ^[0-9a-f]{64}$ ]]; then
    echo "ARMHF CI image $field is not a SHA-256 value: ${value:-missing}" >&2
    exit 1
  fi
}

hash_package_set() {
  dpkg-query -W -f='${binary:Package}=${Version}\n' |
    LC_ALL=C sort |
    sha256sum |
    awk '{print $1}'
}

hash_runtime_set() {
  local library records=""
  for library in "${ARMHF_GCC_RUNTIME_LIBRARIES[@]}"; do
    test -f "$ARMHF_RUNTIME_PREFIX/$library"
    records+="$library=$(sha256sum "$ARMHF_RUNTIME_PREFIX/$library" | awk '{print $1}')"$'\n'
  done
  printf '%s' "$records" | sha256sum | awk '{print $1}'
}

require_armhf_elf() {
  local path=$1 metadata headers attributes
  metadata="$(file -L "$path")"
  headers="$(readelf -hW "$path")"
  attributes="$(readelf -AW "$path")"
  case "$metadata:$headers:$attributes" in
    *"ELF 32-bit"*"ARM, EABI5"*"hard-float ABI"*"Tag_ABI_VFP_args: VFP registers"*) ;;
    *)
      echo "Expected ARM EABI5 hard-float ELF: $path" >&2
      echo "$metadata" >&2
      exit 1
      ;;
  esac
}

verify_glibc_ceiling() {
  local path=$1 versions maximum
  versions="$(readelf --version-info -W "$path")"
  maximum="$(
    awk '
      {
        remaining = $0
        while (match(remaining, /GLIBC_[0-9]+\.[0-9]+/)) {
          version = substr(remaining, RSTART + 6, RLENGTH - 6)
          split(version, parts, ".")
          if (parts[1] > major || (parts[1] == major && parts[2] > minor)) {
            major = parts[1]
            minor = parts[2]
          }
          remaining = substr(remaining, RSTART + RLENGTH)
        }
      }
      END { printf "%d.%d\n", major, minor }
    ' <<< "$versions"
  )"
  if dpkg --compare-versions "$maximum" gt "$ARMHF_GLIBC_VERSION"; then
    echo "$path requires GLIBC_$maximum, newer than Bookworm GLIBC_$ARMHF_GLIBC_VERSION" >&2
    exit 1
  fi
}

reject_quadmath_dependency() {
  local path=$1 dynamic
  dynamic="$(readelf -dW "$path")"
  if [[ "$dynamic" == *"Shared library: [libquadmath.so"* ]]; then
    echo "$path unexpectedly depends on libquadmath" >&2
    exit 1
  fi
}

reject_private_glibc_dependency() {
  local path=$1 versions
  versions="$(readelf --version-info -W "$path")"
  if [[ "$versions" == *"GLIBC_PRIVATE"* ]]; then
    echo "$path unexpectedly depends on a private glibc symbol" >&2
    exit 1
  fi
}

require_equal schema "$ARMHF_CI_IMAGE_SCHEMA" "${schema:-}"
require_equal role "$expected_role" "${role:-}"
require_equal architecture "$expected_arch" "${architecture:-}"
require_equal hamlib_ref "$expected_hamlib_ref" "${hamlib_ref:-}"
require_equal glibc_version "$ARMHF_GLIBC_VERSION" "${glibc_version:-}"
require_equal quadmath_runtime unavailable-on-arm "${quadmath_runtime:-}"
require_sha256 recipe_sha256 "${recipe_sha256:-}"
require_sha256 package_sha256 "${package_sha256:-}"
require_sha256 runtime_sha256 "${runtime_sha256:-}"

profile=armhf-cross-bookworm
if [ "$expected_role" = runtime ]; then
  profile=armhf-runtime-bookworm
fi
if [ -n "${IMAGE_RECIPE_SHA256:-}" ]; then
  expected_recipe=$IMAGE_RECIPE_SHA256
else
  expected_recipe="$(.github/scripts/linux-ci-image-fingerprint.sh "$profile")"
fi
recipe_match=true
if [ "${WSJTX_CI_IMAGE_ALLOW_RECIPE_MISMATCH:-false}" = true ] &&
   [ "$recipe_sha256" != "$expected_recipe" ]; then
  echo "::warning::ARMHF $expected_role image recipe differs from this checkout; using its retained immutable generation" >&2
  recipe_match=false
else
  require_equal recipe_sha256 "$expected_recipe" "$recipe_sha256"
fi

actual_package_sha256="$(hash_package_set)"
require_equal package_sha256 "$package_sha256" "$actual_package_sha256"
require_equal runtime_sha256 "$runtime_sha256" "$(hash_runtime_set)"
for library in "${ARMHF_GCC_RUNTIME_LIBRARIES[@]}"; do
  require_armhf_elf "$ARMHF_RUNTIME_PREFIX/$library"
  verify_glibc_ceiling "$ARMHF_RUNTIME_PREFIX/$library"
  reject_quadmath_dependency "$ARMHF_RUNTIME_PREFIX/$library"
  reject_private_glibc_dependency "$ARMHF_RUNTIME_PREFIX/$library"
done

case "$expected_role" in
  cross-builder)
    compiler="$ARMHF_TOOLCHAIN_PREFIX/bin/$ARMHF_TARGET_TRIPLET-gcc"
    require_equal host_dpkg_arch amd64 "${host_dpkg_arch:-}"
    require_equal compiler_version "$ARMHF_GCC_VERSION" "${compiler_version:-}"
    require_equal compiler_target "$ARMHF_TARGET_TRIPLET" "${compiler_target:-}"
    require_equal binutils_version "$ARMHF_BINUTILS_VERSION" "${binutils_version:-}"
    require_equal linux_headers_version "$ARMHF_LINUX_HEADERS_VERSION" "${linux_headers_version:-}"
    require_equal minimum_kernel_version "$ARMHF_MIN_KERNEL_VERSION" "${minimum_kernel_version:-}"
    require_equal compiler_version "$compiler_version" "$($compiler -dumpfullversion -dumpversion)"
    require_equal compiler_target "$compiler_target" "$($compiler -dumpmachine)"
    compiler_metadata="$(file -L "$compiler")"
    case "$compiler_metadata" in
      *"ELF 64-bit"*"x86-64"*) ;;
      *) echo "Cross compiler is not a native x86-64 executable: $compiler_metadata" >&2; exit 1 ;;
    esac
    signature_helper="$config_dir/linux-ccache-compiler-signature.sh"
    IFS=$'\t' read -r actual_version actual_target actual_sha256 \
      actual_ccache_id actual_signature < <("$signature_helper" "$compiler")
    require_equal compiler_version "$compiler_version" "$actual_version"
    require_equal compiler_target "$compiler_target" "$actual_target"
    require_equal compiler_sha256 "$compiler_sha256" "$actual_sha256"
    require_equal ccache_compatibility_id "$ccache_compatibility_id" "$actual_ccache_id"
    require_equal compiler_signature_sha256 "$compiler_signature_sha256" "$actual_signature"
    require_equal sysroot_package_sha256 "$sysroot_package_sha256" \
      "$(sha256sum /opt/wsjtx-ci/armhf-sysroot-packages.txt | awk '{print $1}')"
    require_equal hamlib_commit "$LINUX_HAMLIB_COMMIT" "${hamlib_commit:-}"
    require_equal hamlib_sha256 "$hamlib_sha256" \
      "$(sha256sum "$ARMHF_SYSROOT/usr/lib/libhamlib.a" | awk '{print $1}')"
    for target_file in \
      "$ARMHF_SYSROOT/usr/bin/rigctl" \
      "$ARMHF_SYSROOT/usr/bin/rigctld" \
      "$ARMHF_SYSROOT/usr/bin/rigctlcom" \
      "$ARMHF_SYSROOT/usr/lib/$ARMHF_TARGET_TRIPLET/libQt5Core.so.5" \
      "$ARMHF_SYSROOT/usr/lib/$ARMHF_TARGET_TRIPLET/qt5/plugins/platforms/libqxcb.so"; do
      require_armhf_elf "$target_file"
      reject_quadmath_dependency "$target_file"
      reject_private_glibc_dependency "$target_file"
    done
    for smoke in /opt/wsjtx-ci/toolchain-smoke/*-smoke; do
      require_armhf_elf "$smoke"
      reject_quadmath_dependency "$smoke"
      reject_private_glibc_dependency "$smoke"
    done
    ;;
  runtime)
    require_equal host_dpkg_arch armhf "$(dpkg --print-architecture)"
    require_equal cross_generation "$generation" "${cross_generation:-}"
    require_sha256 cross_recipe_sha256 "${cross_recipe_sha256:-}"
    if [ -e /opt/wsjtx/hamlib ]; then
      echo "ARMHF runtime image must not provide the build-image Hamlib prefix" >&2
      exit 1
    fi
    qemu_metadata="$(file -L "$ARMHF_QEMU_EXECUTABLE")"
    case "$qemu_metadata" in
      *"ELF 64-bit"*"x86-64"*) ;;
      *) echo "ARMHF AppImage QEMU helper is not x86-64: $qemu_metadata" >&2; exit 1 ;;
    esac
    for plugin in \
      /usr/lib/arm-linux-gnueabihf/qt5/plugins/platforms/libqxcb.so \
      /usr/lib/arm-linux-gnueabihf/qt5/plugins/sqldrivers/libqsqlite.so; do
      require_armhf_elf "$plugin"
    done
    for smoke in /opt/wsjtx-ci/toolchain-smoke/*-smoke; do
      require_armhf_elf "$smoke"
      reject_quadmath_dependency "$smoke"
      reject_private_glibc_dependency "$smoke"
      LD_LIBRARY_PATH="$ARMHF_RUNTIME_PREFIX" OMP_NUM_THREADS=2 "$smoke"
    done
    ;;
  *)
    echo "Unsupported ARMHF image role: $expected_role" >&2
    exit 2
    ;;
esac

echo "ARMHF CI image: $role ${generation:-unknown} (${toolchain_id:-${cross_toolchain_id:-unknown}})"
if [ -n "${GITHUB_OUTPUT:-}" ]; then
  output_prefix=${WSJTX_CI_OUTPUT_PREFIX:-}
  {
    printf '%sgeneration=%s\n' "$output_prefix" "$generation"
    printf '%srecipe_match=%s\n' "$output_prefix" "$recipe_match"
    if [ "$expected_role" = cross-builder ]; then
      printf '%stoolchain_id=%s\n' "$output_prefix" "$toolchain_id"
      printf '%sccache_compatibility_id=%s\n' "$output_prefix" "$ccache_compatibility_id"
      printf '%sccache_compiler_check=string:%s\n' "$output_prefix" "$compiler_signature_sha256"
    else
      printf '%stoolchain_id=%s\n' "$output_prefix" "$cross_toolchain_id"
    fi
  } >> "$GITHUB_OUTPUT"
fi
