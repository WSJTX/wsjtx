#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'USAGE'
Usage: install-r-macos-gfortran.sh --target VERSION --arch ARCH --work-dir PATH --prefix PATH [--github-output]

Downloads the R/macOS GNU Fortran package selected for the requested macOS
build tuple, expands it without using the macOS installer, and prepares a
relocated compiler prefix for CI.
USAGE
}

target=""
arch=""
work_dir=""
prefix=""
github_output=0
pkg_url="https://mac.r-project.org/tools/gfortran-14.2-universal.pkg"
pkg_sha256="ec462d465f093eeee0623d2b5d327bd1038313b985034b766462957e36d7aadd"
version="14.2.0"

while [ "$#" -gt 0 ]; do
  case "$1" in
    --target)
      target="$2"
      shift 2
      ;;
    --arch)
      arch="$2"
      shift 2
      ;;
    --work-dir)
      work_dir="$2"
      shift 2
      ;;
    --prefix)
      prefix="$2"
      shift 2
      ;;
    --github-output)
      github_output=1
      shift
      ;;
    --help|-h)
      usage
      exit 0
      ;;
    *)
      echo "Unknown option: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

if [ -z "$target" ] || [ -z "$arch" ] || [ -z "$work_dir" ] || [ -z "$prefix" ]; then
  usage >&2
  exit 2
fi

case "${arch}:${target}" in
  arm64:11.0)
    triplet="aarch64-apple-darwin20.0"
    ;;
  *)
    echo "::error::Unsupported R/macOS gfortran tuple: arch=${arch}, target=${target}"
    exit 2
    ;;
esac

pkg="${work_dir}/gfortran-14.2-universal.pkg"
expanded="${work_dir}/gfortran-14.2-expanded"
payload_prefix="${expanded}/gfortran.pkg/Payload/opt/gfortran"
runtime_dir="${prefix}/lib/gcc/${triplet}/${version}"

mkdir -p "$work_dir"

echo "R/macOS GNU Fortran install"
echo "Package URL: ${pkg_url}"
echo "Expected SHA-256: ${pkg_sha256}"
echo "Requested tuple: arch=${arch}, target=${target}, triplet=${triplet}"
echo "Install prefix: ${prefix}"

curl -fsSL --retry 3 --retry-delay 5 -o "$pkg" "$pkg_url"
actual_sha256="$(shasum -a 256 "$pkg" | awk '{print $1}')"
echo "Actual SHA-256: ${actual_sha256}"
if [ "$actual_sha256" != "$pkg_sha256" ]; then
  echo "::error::Unexpected R/macOS gfortran package checksum"
  exit 1
fi

pkgutil --expand-full "$pkg" "$expanded"
if [ ! -d "$payload_prefix" ]; then
  echo "::error::Expanded package does not contain expected /opt/gfortran payload"
  exit 1
fi

mkdir -p "$(dirname "$prefix")"
ditto "$payload_prefix" "$prefix"

target_compiler="${prefix}/bin/${triplet}-gfortran"
if [ ! -x "$target_compiler" ]; then
  echo "::error::Expected target gfortran compiler not found: ${target_compiler}"
  exit 1
fi
compiler="$target_compiler"
if [ ! -d "$runtime_dir" ]; then
  echo "::error::Runtime archive directory not found: ${runtime_dir}"
  exit 1
fi

sdkroot="$(xcrun --sdk macosx --show-sdk-path)"
libgfortran="${runtime_dir}/libgfortran.a"
libquadmath="${runtime_dir}/libquadmath.a"
libgcc="${runtime_dir}/libgcc.a"
libgomp="${runtime_dir}/libgomp.a"

archives=("$libgfortran" "$libquadmath" "$libgcc" "$libgomp")
for archive in "${archives[@]}"; do
  if [ ! -f "$archive" ]; then
    echo "::error::Expected runtime archive not found: ${archive}"
    exit 1
  fi
done

"${GITHUB_WORKSPACE:-$(pwd)}/.github/scripts/verify-macos-static-archives.sh" \
  --target "$target" \
  --arch "$arch" \
  --mode error \
  "${archives[@]}"

echo "Compiler: ${compiler}"
file "$compiler"
otool -L "$compiler"

runtime_link_flags="${libgfortran} ${libquadmath} ${libgcc}"
fortran_runtime_flags="-static-libgcc -static-libgfortran -static-libquadmath -isysroot ${sdkroot}"
runtime_archives="${archives[*]}"
cache_key="r-macos-gfortran-14.2-universal-${pkg_sha256}"

if [ "$github_output" -eq 1 ]; then
  github_output_file="${GITHUB_OUTPUT:-/dev/stdout}"
  {
    echo "path=${compiler}"
    echo "prefix=${prefix}"
    echo "original_prefix=/opt/gfortran"
    echo "runtime_lib_dir=${runtime_dir}"
    echo "fortran_runtime_flags=${fortran_runtime_flags}"
    echo "runtime_link_flags=${runtime_link_flags}"
    echo "runtime_archives=${runtime_archives}"
    echo "libgomp=${libgomp}"
    echo "version=${version}"
    echo "cache_key=${cache_key}"
  } >> "$github_output_file"
fi
