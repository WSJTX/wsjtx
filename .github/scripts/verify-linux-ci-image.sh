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

config_dir=/usr/local/share/wsjtx-ci
if [ ! -f "$config_dir/linux-ci-image-config.sh" ]; then
  config_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
fi
# shellcheck source=.github/scripts/linux-ci-image-config.sh
. "$config_dir/linux-ci-image-config.sh"
if [ "$expected_flavor" = tsan ]; then
  # shellcheck source=.github/scripts/tsan-linux-deps-config.sh
  . "$config_dir/tsan-linux-deps-config.sh"
fi

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

require_sha256() {
  local field=$1 value=$2
  if [[ ! "$value" =~ ^[0-9a-f]{64}$ ]]; then
    echo "Linux CI image $field is not a SHA-256 value: ${value:-missing}" >&2
    exit 1
  fi
}

require_key_component() {
  local field=$1 value=$2
  if [[ ! "$value" =~ ^[A-Za-z0-9][A-Za-z0-9._-]*$ ]]; then
    echo "Linux CI image $field is not a safe cache-key component: ${value:-missing}" >&2
    exit 1
  fi
}

require_equal schema 1 "${schema:-}"
require_equal flavor "$expected_flavor" "${flavor:-}"
require_equal architecture "$expected_arch" "${architecture:-}"
require_equal hamlib_ref "$expected_hamlib_ref" "${hamlib_ref:-}"
require_key_component generation "${generation:-}"

case "$expected_flavor" in
  normal)
    expected_compiler=gcc
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
    if [ "${WSJTX_CI_IMAGE_SKIP_RUNTIME_CHECKS:-false}" != true ]; then
      qt_core_config="$(find /usr -path '*/cmake/Qt5Core/Qt5CoreConfig.cmake' -print -quit)"
      qt_multimedia_config="$(find /usr -path '*/cmake/Qt5Multimedia/Qt5MultimediaConfig.cmake' -print -quit)"
      qt_plugin="$(find /usr -path '*/plugins/platforms/*' -type f -print -quit)"
      test -n "$qt_core_config"
      test -n "$qt_multimedia_config"
      test -n "$qt_plugin"
    fi
    pfunit_dir="$(dirname "$pfunit_config")"
    hamlib_prefix=$image_prefix/hamlib
    ;;
  tsan)
    expected_compiler=gcc-13
    profile=tsan-noble
    hamlib_prefix=$image_prefix/tsan/hamlib
    require_equal hamlib_commit "$TSAN_HAMLIB_COMMIT" "${hamlib_commit:-}"
    require_equal hamlib_patch_commit "$TSAN_HAMLIB_PATCH_COMMIT" "${hamlib_patch_commit:-}"
    require_equal qt_version "$TSAN_QT_VERSION" "${qt_version:-}"
    require_equal boost_version "$TSAN_BOOST_VERSION" "${boost_version:-}"
    "$config_dir/verify-tsan-deps-linux.sh" \
      "$image_prefix/tsan/qt" "$image_prefix/tsan/boost" "$image_prefix/tsan/hamlib"
    boost_config="$(find "$image_prefix/tsan/boost" -name BoostConfig.cmake -print -quit)"
    test -n "$boost_config"
    if [ "${WSJTX_CI_IMAGE_SKIP_RUNTIME_CHECKS:-false}" != true ]; then
      test -d "$image_prefix/tsan/qt/plugins"
      test -n "$(find "$image_prefix/tsan/qt/plugins" -type f -print -quit)"
    fi
    ;;
  *)
    echo "Unsupported Linux CI image flavor: $expected_flavor" >&2
    exit 2
    ;;
esac

if [ -n "${IMAGE_RECIPE_SHA256:-}" ]; then
  expected_recipe=$IMAGE_RECIPE_SHA256
else
  expected_recipe="$(.github/scripts/linux-ci-image-fingerprint.sh "$profile")"
fi
toolchain_recipe=$expected_recipe
recipe_match=true
if [ "${WSJTX_CI_IMAGE_ALLOW_RECIPE_MISMATCH:-false}" = true ] &&
   [ "${recipe_sha256:-}" != "$expected_recipe" ]; then
  echo "::warning::Linux CI image recipe fingerprint differs from this checkout; using the last known-good image generation" >&2
  if [ -n "${GITHUB_STEP_SUMMARY:-}" ]; then
    {
      echo "### Linux CI image fallback"
      echo
      echo "This job used a previously promoted image while the dependency refresh was in progress."
      echo
      echo "- Image recipe in the image: \`${recipe_sha256:-missing}\`"
      echo "- Recipe in this checkout: \`$expected_recipe\`"
    } >> "$GITHUB_STEP_SUMMARY"
  fi
  toolchain_recipe=${recipe_sha256:-}
  recipe_match=false
else
  require_equal recipe_sha256 "$expected_recipe" "${recipe_sha256:-}"
fi
require_equal compiler "$expected_compiler" "${compiler:-}"
test -n "${compiler:-}"
test -n "${compiler_version:-}"
test -n "${compiler_sha256:-}"
test -n "${compiler_target:-}"
test -n "${package_sha256:-}"
require_key_component toolchain_id "${toolchain_id:-}"
if [[ ! "${compiler_version:-}" =~ ^[0-9]+([.][0-9]+)*$ ]]; then
  echo "Linux CI image compiler_version is invalid: ${compiler_version:-missing}" >&2
  exit 1
fi
require_key_component compiler_target "${compiler_target:-}"
require_sha256 compiler_sha256 "${compiler_sha256:-}"
require_sha256 package_sha256 "${package_sha256:-}"
require_sha256 recipe_sha256 "${recipe_sha256:-}"

if [ "$recipe_match" = true ]; then
  compiler_major=${compiler_version%%.*}
  require_equal ccache_compatibility_id \
    "gcc${compiler_major}-v2" "${ccache_compatibility_id:-}"
  require_key_component ccache_compatibility_id "$ccache_compatibility_id"
  require_sha256 compiler_signature_sha256 "${compiler_signature_sha256:-}"
  verified_ccache_compatibility_id=$ccache_compatibility_id
  verified_ccache_compiler_check="string:$compiler_signature_sha256"
else
  verified_ccache_compatibility_id=$toolchain_id
  verified_ccache_compiler_check=mtime
fi

if [ "${WSJTX_CI_IMAGE_SKIP_RUNTIME_CHECKS:-false}" != true ]; then
  compiler_path="$(readlink -f "$(command -v "$compiler")")"
  require_equal dpkg_arch "$(dpkg --print-architecture)" "${dpkg_arch:-}"
  require_equal compiler_version "$compiler_version" "$($compiler -dumpfullversion -dumpversion)"
  require_equal compiler_target "$compiler_target" "$($compiler -dumpmachine)"
  require_equal compiler_sha256 "$compiler_sha256" "$(sha256sum "$compiler_path" | awk '{print $1}')"
  actual_package_sha256="$(dpkg-query -W -f='${binary:Package}=${Version}\n' | LC_ALL=C sort | sha256sum | awk '{print $1}')"
  require_equal package_sha256 "$package_sha256" "$actual_package_sha256"
  toolchain_identity="$toolchain_recipe:$compiler_target:$compiler_sha256:$package_sha256"
  toolchain_sha256="$(printf '%s' "$toolchain_identity" | sha256sum | awk '{print $1}')"
  require_equal toolchain_id "$toolchain_id" "gcc${compiler_version}-${expected_arch}-${toolchain_sha256:0:20}"

  if [ "$recipe_match" = true ]; then
    signature_helper="$config_dir/linux-ccache-compiler-signature.sh"
    if [ ! -x "$signature_helper" ]; then
      echo "Linux ccache compiler signature helper not found: $signature_helper" >&2
      exit 1
    fi
    IFS=$'\t' read -r actual_compiler_version actual_compiler_target \
      actual_compiler_sha256 actual_ccache_compatibility_id \
      actual_compiler_signature_sha256 \
      < <("$signature_helper" "$compiler")
    require_equal compiler_version "$compiler_version" "$actual_compiler_version"
    require_equal compiler_target "$compiler_target" "$actual_compiler_target"
    require_equal compiler_sha256 "$compiler_sha256" "$actual_compiler_sha256"
    require_equal ccache_compatibility_id "$ccache_compatibility_id" \
      "$actual_ccache_compatibility_id"
    require_equal compiler_signature_sha256 "$compiler_signature_sha256" \
      "$actual_compiler_signature_sha256"
  fi
fi

if [ "${WSJTX_CI_IMAGE_SKIP_RUNTIME_CHECKS:-false}" != true ]; then
  smoke_dir="$(mktemp -d)"
  cat > "$smoke_dir/CMakeLists.txt" <<'EOF'
cmake_minimum_required(VERSION 3.16)
project(wsjt_ci_image_smoke LANGUAGES CXX)

find_package(Qt5 REQUIRED COMPONENTS Core)
find_package(Boost REQUIRED COMPONENTS log_setup log)
find_package(PkgConfig REQUIRED)
set(PKG_CONFIG_USE_STATIC_LIBS TRUE)
pkg_check_modules(HAMLIB REQUIRED IMPORTED_TARGET hamlib)

add_executable(wsjt_ci_image_smoke main.cpp)
target_include_directories(wsjt_ci_image_smoke PRIVATE "${WSJTX_HAMLIB_PREFIX}/include")
target_link_libraries(wsjt_ci_image_smoke PRIVATE Qt5::Core Boost::log_setup Boost::log PkgConfig::HAMLIB)
EOF
  cat > "$smoke_dir/main.cpp" <<'EOF'
#include <QCoreApplication>
#include <boost/log/trivial.hpp>
#include <hamlib/rig.h>

int main(int argc, char **argv)
{
  QCoreApplication application(argc, argv);
  BOOST_LOG_TRIVIAL(info) << "WSJT-X CI image smoke test";
  return 0;
}
EOF

  cmake_args=(
    -S "$smoke_dir"
    -B "$smoke_dir/build"
    -DCMAKE_BUILD_TYPE=Release
    "-DWSJTX_HAMLIB_PREFIX=$hamlib_prefix"
  )
  if [ "$expected_flavor" = tsan ]; then
    cmake_args+=(
      "-DCMAKE_PREFIX_PATH=$image_prefix/tsan/qt;$image_prefix/tsan/boost;$image_prefix/tsan/hamlib"
      "-DBOOST_ROOT=$image_prefix/tsan/boost"
      -DBoost_NO_SYSTEM_PATHS=ON
      -DCMAKE_CXX_FLAGS=-fsanitize=thread
      -DCMAKE_EXE_LINKER_FLAGS=-fsanitize=thread
    )
  fi
  export PKG_CONFIG_PATH="$hamlib_prefix/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
  cmake "${cmake_args[@]}"
  cmake --build "$smoke_dir/build" --parallel "$(nproc)"
  rm -rf "$smoke_dir"
fi

echo "Linux CI image: ${generation:-unknown} (${toolchain_id:-unknown}; ccache ${verified_ccache_compatibility_id:-unknown})"

if [ -n "${GITHUB_OUTPUT:-}" ]; then
  {
    printf 'generation=%s\n' "$generation"
    printf 'toolchain_id=%s\n' "$toolchain_id"
    printf 'ccache_compatibility_id=%s\n' "$verified_ccache_compatibility_id"
    printf 'ccache_compiler_check=%s\n' "$verified_ccache_compiler_check"
    printf 'recipe_match=%s\n' "$recipe_match"
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
