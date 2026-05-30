#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 2 ]; then
  echo "Usage: build-pfunit-windows.sh TAG PREFIX" >&2
  exit 2
fi

tag="$1"
prefix="$2"

git config --global core.longpaths true
git clone --depth 1 --branch "$tag" --recursive \
  https://github.com/Goddard-Fortran-Ecosystem/pFUnit.git pfunit-src
cmake -S pfunit-src -B pfunit-build -G "MSYS Makefiles" \
  -DSKIP_MPI=YES \
  -DSKIP_OPENMP=YES \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
  -DCMAKE_INSTALL_PREFIX="$prefix"
cmake --build pfunit-build -j"$(nproc)"
cmake --install pfunit-build
