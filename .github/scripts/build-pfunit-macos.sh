#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -lt 4 ] || [ "$#" -gt 6 ]; then
  echo "Usage: build-pfunit-macos.sh TAG FORTRAN_COMPILER DEPLOYMENT_TARGET PREFIX [FORTRAN_FLAGS] [LINKER_FLAGS]" >&2
  exit 2
fi

tag="$1"
fortran_compiler="$2"
deployment_target="$3"
prefix="$4"
fortran_flags="${5:-}"
linker_flags="${6:-}"

git clone --depth 1 --branch "$tag" --recursive \
  https://github.com/Goddard-Fortran-Ecosystem/pFUnit.git pfunit-src

gftl_shared_v1_cmake="pfunit-src/extern/fArgParse/extern/gFTL-shared/src/v1/CMakeLists.txt"
if [ -f "$gftl_shared_v1_cmake" ]; then
  gftl_shared_v1_cmake_tmp="${gftl_shared_v1_cmake}.tmp"
  awk '
    $0 == "add_executable (demo.x demo.F90)" {
      print "add_executable (demo.x EXCLUDE_FROM_ALL demo.F90)"
      replaced = 1
      next
    }
    { print }
    END { if (!replaced) exit 42 }
  ' "$gftl_shared_v1_cmake" > "$gftl_shared_v1_cmake_tmp" || {
    echo "::error::Could not exclude gFTL-shared demo.x from the pFUnit dependency build"
    exit 1
  }
  mv "$gftl_shared_v1_cmake_tmp" "$gftl_shared_v1_cmake"
fi

cmake -S pfunit-src -B pfunit-build \
  -DSKIP_MPI=YES \
  -DSKIP_OPENMP=YES \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
  -DCMAKE_C_COMPILER=/usr/bin/cc \
  -DCMAKE_Fortran_COMPILER="$fortran_compiler" \
  -DCMAKE_Fortran_FLAGS="$fortran_flags" \
  -DCMAKE_EXE_LINKER_FLAGS="$linker_flags" \
  -DCMAKE_OSX_DEPLOYMENT_TARGET="$deployment_target" \
  -DCMAKE_INSTALL_PREFIX="$prefix"
cmake --build pfunit-build -j"$(sysctl -n hw.ncpu)"
cmake --install pfunit-build
