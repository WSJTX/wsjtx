#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 3 ]; then
  echo "Usage: build-armhf-toolchain-smoke.sh TOOLCHAIN_PREFIX SYSROOT OUTPUT_DIR" >&2
  exit 2
fi

toolchain_prefix=$1
sysroot=$2
output_dir=$3
triplet=arm-linux-gnueabihf
compiler_prefix="$toolchain_prefix/bin/$triplet"
target_include="$sysroot/usr/include/$triplet"
target_lib="$sysroot/usr/lib/$triplet"
target_base_lib="$sysroot/lib/$triplet"
common_flags=(
  "--sysroot=$sysroot"
  -isystem "$target_include"
  "-B$target_lib/"
  "-L$target_lib"
  "-Wl,-rpath-link,$target_lib"
  "-Wl,-rpath-link,$target_base_lib"
)

mkdir -p "$output_dir"
source_dir="$(mktemp -d)"
trap 'rm -rf "$source_dir"' EXIT

cat > "$source_dir/c.c" <<'EOF'
#include <stdatomic.h>
#include <stdio.h>
int main(void) {
  _Atomic long long value = 41;
  printf("c atomic: %lld\n", atomic_fetch_add(&value, 1) + 1);
  return 0;
}
EOF
cat > "$source_dir/cxx.cpp" <<'EOF'
#include <iostream>
#include <numeric>
#include <vector>
int main() {
  std::vector<int> values{1, 2, 3, 4};
  std::cout << "c++ standard library: "
            << std::accumulate(values.begin(), values.end(), 0) << '\n';
  return 0;
}
EOF
cat > "$source_dir/fortran.f90" <<'EOF'
program fortran_smoke
  use omp_lib
  implicit none
  integer :: i, total
  total = 0
  !$omp parallel do reduction(+:total)
  do i = 1, 4
    total = total + i
  end do
  !$omp end parallel do
  print '(A,I0)', 'fortran openmp: ', total
end program fortran_smoke
EOF

"${compiler_prefix}-gcc" "${common_flags[@]}" "$source_dir/c.c" -latomic \
  -o "$output_dir/c-smoke"
"${compiler_prefix}-g++" "${common_flags[@]}" "$source_dir/cxx.cpp" \
  -o "$output_dir/cxx-smoke"
"${compiler_prefix}-gfortran" "${common_flags[@]}" -fopenmp \
  "$source_dir/fortran.f90" -o "$output_dir/fortran-smoke"

for executable in "$output_dir"/*-smoke; do
  metadata="$(file "$executable")"
  headers="$("${compiler_prefix}-readelf" -hW "$executable")"
  programs="$("${compiler_prefix}-readelf" -lW "$executable")"
  case "$metadata:$headers:$programs" in
    *"ELF 32-bit"*"ARM, EABI5"*"hard-float ABI"*"/lib/ld-linux-armhf.so.3"*) ;;
    *)
      echo "ARMHF toolchain smoke output has invalid ABI metadata: $executable" >&2
      exit 1
      ;;
  esac
done
