#!/usr/bin/env bash

# Shared, pinned build tuple for Linux ThreadSanitizer dependencies.
# shellcheck disable=SC2034

TSAN_CACHE_EPOCH=1
TSAN_RUNTIME_BASE=ubuntu24.04
TSAN_ARCH=x86_64

TSAN_GCC_SUFFIX=13
TSAN_GCC_VERSION=13.3.0
TSAN_CC=gcc-${TSAN_GCC_SUFFIX}
TSAN_CXX=g++-${TSAN_GCC_SUFFIX}
TSAN_FC=gfortran-${TSAN_GCC_SUFFIX}
TSAN_SANITIZER_FLAGS="-fsanitize=thread -fno-omit-frame-pointer -g1 -O1"
TSAN_LINK_FLAGS="-fsanitize=thread"

TSAN_QT_VERSION=5.15.18
TSAN_QT_SOURCE_SHA256=cea1fbabf02455f3f0e8eaa839f5d6f45cdb56b62c8a83af5c1d00ac05f912ea
TSAN_QT_SOURCE_URL="https://download.qt.io/archive/qt/5.15/${TSAN_QT_VERSION}/single/qt-everywhere-opensource-src-${TSAN_QT_VERSION}.tar.xz"

TSAN_BOOST_VERSION=1.83.0
TSAN_BOOST_SOURCE_SHA256=6478edfe2f3305127cffe8caf73ea0176c53769f4bf1585be237eb30798c3b8e
TSAN_BOOST_SOURCE_URL="https://archives.boost.io/release/${TSAN_BOOST_VERSION}/source/boost_1_83_0.tar.bz2"

TSAN_HAMLIB_REF=4.7.2
TSAN_HAMLIB_COMMIT=40f63488fe0bd751b147f48d62fd217bf53713a0
TSAN_HAMLIB_PATCH_COMMIT=141e1b19a0e2d65571e5b0783c43ab2615c0cdf7

tsan_config_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"

tsan_sha256_files() {
  local file digest hashes=

  for file in "$@"; do
    if [ ! -f "$file" ]; then
      echo "Cannot hash missing file: $file" >&2
      return 1
    fi
    digest="$(sha256sum "$file" | awk '{print $1}')"
    hashes+="${digest}"$'\n'
  done
  printf '%s' "$hashes" | sha256sum | awk '{print $1}'
}

tsan_require_toolchain() {
  local compiler actual_version linker_path

  for compiler in "$TSAN_CC" "$TSAN_CXX" "$TSAN_FC"; do
    if ! command -v "$compiler" >/dev/null 2>&1; then
      echo "Pinned TSan compiler not found: $compiler" >&2
      return 1
    fi
    actual_version="$($compiler -dumpfullversion -dumpversion)"
    if [ "$actual_version" != "$TSAN_GCC_VERSION" ]; then
      echo "Pinned TSan compiler mismatch for $compiler: expected $TSAN_GCC_VERSION, got $actual_version" >&2
      return 1
    fi
  done

  TSAN_TARGET="$($TSAN_CC -dumpmachine)"
  TSAN_CC_PATH="$(readlink -f "$(command -v "$TSAN_CC")")"
  TSAN_CXX_PATH="$(readlink -f "$(command -v "$TSAN_CXX")")"
  TSAN_FC_PATH="$(readlink -f "$(command -v "$TSAN_FC")")"
  linker_path="$($TSAN_CC -print-prog-name=ld)"
  if [[ "$linker_path" != */* ]]; then
    linker_path="$(command -v "$linker_path")"
  fi
  TSAN_LINKER_PATH="$(readlink -f "$linker_path")"
  TSAN_LIBTSAN_PATH="$(readlink -f "$($TSAN_CC -print-file-name=libtsan.so)")"
  TSAN_LIBSTDCXX_PATH="$(readlink -f "$($TSAN_CXX -print-file-name=libstdc++.so)")"
  TSAN_LIBGCC_PATH="$(readlink -f "$($TSAN_CC -print-file-name=libgcc_s.so)")"

  for compiler in \
    "$TSAN_CC_PATH" "$TSAN_CXX_PATH" "$TSAN_FC_PATH" "$TSAN_LINKER_PATH" \
    "$TSAN_LIBTSAN_PATH" "$TSAN_LIBSTDCXX_PATH" "$TSAN_LIBGCC_PATH"; do
    if [ ! -f "$compiler" ]; then
      echo "Pinned TSan toolchain component not found: $compiler" >&2
      return 1
    fi
  done

  TSAN_GLIBC_VERSION="$(getconf GNU_LIBC_VERSION | tr ' ' '-')"
  TSAN_COMPILER_SHA256="$(tsan_sha256_files \
    "$TSAN_CC_PATH" "$TSAN_CXX_PATH" "$TSAN_FC_PATH" "$TSAN_LINKER_PATH")"
  TSAN_RUNTIME_SHA256="$({
    printf '%s\n' "$TSAN_GCC_VERSION" "$TSAN_TARGET" "$TSAN_GLIBC_VERSION"
    sha256sum "$TSAN_LIBTSAN_PATH" "$TSAN_LIBSTDCXX_PATH" "$TSAN_LIBGCC_PATH"
  } | sha256sum | awk '{print $1}')"
  TSAN_TOOLCHAIN_ID="gcc${TSAN_GCC_VERSION}-${TSAN_TARGET}-${TSAN_COMPILER_SHA256:0:12}-${TSAN_RUNTIME_SHA256:0:12}"
}

tsan_dependency_recipe_hash() {
  local dependency=$1 build_script

  case "$dependency" in
    qt) build_script=build-qt-tsan-linux.sh ;;
    boost) build_script=build-boost-tsan-linux.sh ;;
    hamlib) build_script=build-hamlib-tsan-linux.sh ;;
    *)
      echo "Unsupported TSan dependency: $dependency" >&2
      return 2
      ;;
  esac

  tsan_sha256_files \
    "${tsan_config_dir}/tsan-linux-deps-config.sh" \
    "${tsan_config_dir}/${build_script}" \
    "${tsan_config_dir}/verify-tsan-deps-linux.sh" \
    "${tsan_config_dir}/../actions/prepare-linux-tsan-deps/action.yml"
}

tsan_dependency_version() {
  case "$1" in
    qt) printf '%s\n' "$TSAN_QT_VERSION" ;;
    boost) printf '%s\n' "$TSAN_BOOST_VERSION" ;;
    hamlib) printf '%s\n' "$TSAN_HAMLIB_REF" ;;
    *) return 2 ;;
  esac
}

tsan_dependency_source_id() {
  case "$1" in
    qt) printf '%s\n' "$TSAN_QT_SOURCE_SHA256" ;;
    boost) printf '%s\n' "$TSAN_BOOST_SOURCE_SHA256" ;;
    hamlib) printf '%s+%s\n' "$TSAN_HAMLIB_COMMIT" "$TSAN_HAMLIB_PATCH_COMMIT" ;;
    *) return 2 ;;
  esac
}

tsan_dependency_cache_key() {
  local dependency=$1 version source_id recipe_hash

  if [ -z "${TSAN_TOOLCHAIN_ID:-}" ]; then
    tsan_require_toolchain
  fi
  version="$(tsan_dependency_version "$dependency")"
  source_id="$(tsan_dependency_source_id "$dependency")"
  recipe_hash="$(tsan_dependency_recipe_hash "$dependency")"
  printf '%s\n' "${dependency}-tsan-linux-${TSAN_ARCH}-${TSAN_RUNTIME_BASE}-${TSAN_TOOLCHAIN_ID}-${version}-${source_id}-epoch${TSAN_CACHE_EPOCH}-${recipe_hash}"
}

tsan_write_dependency_manifest() {
  local dependency=$1 prefix=$2 version source_id recipe_hash cache_key manifest

  tsan_require_toolchain
  version="$(tsan_dependency_version "$dependency")"
  source_id="$(tsan_dependency_source_id "$dependency")"
  recipe_hash="$(tsan_dependency_recipe_hash "$dependency")"
  cache_key="$(tsan_dependency_cache_key "$dependency")"
  manifest="${prefix}/.wsjtx-tsan-manifest"

  {
    printf 'schema=%s\n' "$TSAN_CACHE_EPOCH"
    printf 'dependency=%s\n' "$dependency"
    printf 'version=%s\n' "$version"
    printf 'source_id=%s\n' "$source_id"
    printf 'runtime_base=%s\n' "$TSAN_RUNTIME_BASE"
    printf 'architecture=%s\n' "$TSAN_ARCH"
    printf 'compiler=%s\n' "$TSAN_CC"
    printf 'compiler_version=%s\n' "$TSAN_GCC_VERSION"
    printf 'compiler_target=%s\n' "$TSAN_TARGET"
    printf 'compiler_sha256=%s\n' "$TSAN_COMPILER_SHA256"
    printf 'glibc_version=%s\n' "$TSAN_GLIBC_VERSION"
    printf 'runtime_sha256=%s\n' "$TSAN_RUNTIME_SHA256"
    printf 'recipe_sha256=%s\n' "$recipe_hash"
    printf 'cache_key=%s\n' "$cache_key"
    printf 'sanitizer_flags=%s\n' "$TSAN_SANITIZER_FLAGS"
    printf 'link_flags=%s\n' "$TSAN_LINK_FLAGS"
  } > "$manifest"
}

tsan_emit_github_output() {
  if [ -z "${GITHUB_OUTPUT:-}" ]; then
    echo "GITHUB_OUTPUT is required for --github-output" >&2
    return 2
  fi

  tsan_require_toolchain
  {
    printf 'cc=%s\n' "$TSAN_CC"
    printf 'cxx=%s\n' "$TSAN_CXX"
    printf 'fc=%s\n' "$TSAN_FC"
    printf 'gcc_version=%s\n' "$TSAN_GCC_VERSION"
    printf 'toolchain_id=%s\n' "$TSAN_TOOLCHAIN_ID"
    printf 'qt_key=%s\n' "$(tsan_dependency_cache_key qt)"
    printf 'boost_key=%s\n' "$(tsan_dependency_cache_key boost)"
    printf 'hamlib_key=%s\n' "$(tsan_dependency_cache_key hamlib)"
    printf 'hamlib_ref=%s\n' "$TSAN_HAMLIB_REF"
  } >> "$GITHUB_OUTPUT"
}

if [ "${BASH_SOURCE[0]}" = "$0" ]; then
  set -euo pipefail
  case "${1:-}" in
    --github-output)
      tsan_emit_github_output
      ;;
    --print)
      tsan_require_toolchain
      printf 'toolchain_id=%s\n' "$TSAN_TOOLCHAIN_ID"
      printf 'qt_key=%s\n' "$(tsan_dependency_cache_key qt)"
      printf 'boost_key=%s\n' "$(tsan_dependency_cache_key boost)"
      printf 'hamlib_key=%s\n' "$(tsan_dependency_cache_key hamlib)"
      ;;
    *)
      echo "Usage: $0 --github-output|--print" >&2
      exit 2
      ;;
  esac
fi
