#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 4 ]; then
  echo "Usage: build-pfunit-macos.sh TAG FORTRAN_COMPILER DEPLOYMENT_TARGET PREFIX" >&2
  exit 2
fi

tag="$1"
fortran_compiler="$2"
deployment_target="$3"
prefix="$4"

git clone --depth 1 --branch "$tag" --recursive \
  https://github.com/Goddard-Fortran-Ecosystem/pFUnit.git pfunit-src
cmake -S pfunit-src -B pfunit-build \
  -DSKIP_MPI=YES \
  -DSKIP_OPENMP=YES \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
  -DCMAKE_Fortran_COMPILER="$fortran_compiler" \
  -DCMAKE_OSX_DEPLOYMENT_TARGET="$deployment_target" \
  -DCMAKE_INSTALL_PREFIX="$prefix"
cmake --build pfunit-build -j"$(sysctl -n hw.ncpu)"
cmake --install pfunit-build
