#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'USAGE'
Usage: install-xpack-gfortran-macos.sh --target VERSION --arch ARCH --work-dir PATH --prefix PATH [--github-output]

Downloads the xPack GCC archive selected for the requested macOS build tuple,
verifies it, and prepares a relocated compiler prefix for CI.
USAGE
}

target=""
arch=""
work_dir=""
prefix=""
github_output=0

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
  x86_64:10.13)
    archive_name="xpack-gcc-12.4.0-1-darwin-x64"
    package_name="xpack-gcc-12.4.0-1"
    package_url="https://github.com/xpack-dev-tools/gcc-xpack/releases/download/v12.4.0-1/${archive_name}.tar.gz"
    package_sha256="f194b0fc72c7d6563694707794e350e73f807635c8bdb05168924a56126d871c"
    runtime_dir="lib/gcc/x86_64-apple-darwin18.7.0/12.4.0"
    version="12.4.0"
    ;;
  *)
    echo "::error::Unsupported xPack gfortran tuple: arch=${arch}, target=${target}"
    exit 2
    ;;
esac

archive="${work_dir}/${archive_name}.tar.gz"
expanded="${work_dir}/expanded"
payload_prefix="${expanded}/${package_name}"

mkdir -p "$work_dir"

echo "xPack GCC install"
echo "Package URL: ${package_url}"
echo "Expected SHA-256: ${package_sha256}"
echo "Requested tuple: arch=${arch}, target=${target}"
echo "Install prefix: ${prefix}"

curl -fsSL --retry 3 --retry-delay 5 -o "$archive" "$package_url"
actual_sha256="$(shasum -a 256 "$archive" | awk '{print $1}')"
echo "Actual SHA-256: ${actual_sha256}"
if [ "$actual_sha256" != "$package_sha256" ]; then
  echo "::error::Unexpected xPack GCC archive checksum"
  exit 1
fi

mkdir -p "$expanded"
tar -xzf "$archive" -C "$expanded"
if [ ! -d "$payload_prefix" ]; then
  echo "::error::Expanded archive does not contain expected ${package_name} payload"
  exit 1
fi

mkdir -p "$(dirname "$prefix")"
ditto "$payload_prefix" "$prefix"

compiler="${prefix}/bin/gfortran"
if [ ! -x "$compiler" ]; then
  echo "::error::Expected gfortran compiler not found: ${compiler}"
  exit 1
fi

sdkroot="$(xcrun --sdk macosx --show-sdk-path)"
libgfortran="${prefix}/lib/libgfortran.a"
libquadmath="${prefix}/lib/libquadmath.a"
libgcc="${prefix}/${runtime_dir}/libgcc.a"
libgomp="${prefix}/lib/libgomp.a"
libgfortran_spec="${prefix}/lib/libgfortran.spec"

archives=("$libgfortran" "$libquadmath" "$libgcc" "$libgomp")
for archive_member in "${archives[@]}"; do
  if [ ! -f "$archive_member" ]; then
    echo "::error::Expected runtime archive not found: ${archive_member}"
    exit 1
  fi
done

if [ ! -f "$libgfortran_spec" ]; then
  echo "::error::Expected libgfortran spec not found: ${libgfortran_spec}"
  exit 1
fi

spec_tmp="${libgfortran_spec}.tmp"
# This xPack Darwin build lacks -static-libquadmath, but its libgfortran spec
# injects -lquadmath. Rewrite that injected library to the verified static
# archive so the compiler driver can still own the Fortran runtime link policy.
sed "s|-lquadmath|${libquadmath}|g" "$libgfortran_spec" > "$spec_tmp"
if cmp -s "$libgfortran_spec" "$spec_tmp"; then
  echo "::error::xPack libgfortran spec did not contain the expected libquadmath injection"
  exit 1
fi
mv "$spec_tmp" "$libgfortran_spec"

"${GITHUB_WORKSPACE:-$(pwd)}/.github/scripts/verify-macos-static-archives.sh" \
  --target "$target" \
  --arch "$arch" \
  --mode error \
  "${archives[@]}"

echo "Compiler: ${compiler}"
file "$compiler"
otool -L "$compiler"
"$compiler" --version

runtime_link_flags=""
fortran_runtime_flags="-static-libgcc -static-libgfortran -mmacosx-version-min=${target} -isysroot ${sdkroot}"
runtime_archives="${archives[*]}"
cache_key="${archive_name}-${package_sha256}"

quadmath_probe="${work_dir}/quadmath-static-probe.f90"
quadmath_probe_exe="${work_dir}/quadmath-static-probe"
{
  echo "program main"
  echo "  real(16) :: x"
  echo "  x = sqrt(2.0_16)"
  echo "  print *, x"
  echo "end program main"
} > "$quadmath_probe"

"$compiler" \
  -static-libgcc \
  -static-libgfortran \
  "-mmacosx-version-min=${target}" \
  -isysroot "$sdkroot" \
  "$quadmath_probe" \
  -o "$quadmath_probe_exe"

otool -L "$quadmath_probe_exe"
if otool -L "$quadmath_probe_exe" | awk '/libquadmath.*\.dylib/ { found=1; print } END { exit found ? 0 : 1 }'; then
  echo "::error::xPack gfortran linked the quadmath probe against dynamic libquadmath"
  exit 1
fi

if [ "$github_output" -eq 1 ]; then
  github_output_file="${GITHUB_OUTPUT:-/dev/stdout}"
  {
    echo "path=${compiler}"
    echo "prefix=${prefix}"
    echo "original_prefix=xpack-gcc"
    echo "runtime_lib_dir=${prefix}/${runtime_dir}"
    echo "fortran_runtime_flags=${fortran_runtime_flags}"
    echo "runtime_link_flags=${runtime_link_flags}"
    echo "runtime_archives=${runtime_archives}"
    echo "libgomp=${libgomp}"
    echo "version=${version}"
    echo "cache_key=${cache_key}"
  } >> "$github_output_file"
fi
