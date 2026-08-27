#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 2 ]; then
  echo "Usage: build-pfunit-linux.sh REF PREFIX" >&2
  exit 2
fi

ref="$1"
prefix="$2"

git init pfunit-src
git -C pfunit-src remote add origin \
  https://github.com/Goddard-Fortran-Ecosystem/pFUnit.git
git -C pfunit-src fetch --depth 1 origin "$ref"
git -C pfunit-src checkout --detach FETCH_HEAD
actual_commit="$(git -C pfunit-src rev-parse HEAD)"
if [[ "$ref" =~ ^[0-9a-f]{40}$ ]] && [ "$actual_commit" != "$ref" ]; then
  echo "pFUnit source mismatch: expected $ref, got $actual_commit" >&2
  exit 1
fi
git -C pfunit-src submodule update --init --recursive --depth 1
cmake -S pfunit-src -B pfunit-build \
  -DSKIP_MPI=YES \
  -DSKIP_OPENMP=YES \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
  -DCMAKE_INSTALL_PREFIX="$prefix"
cmake --build pfunit-build -j"$(nproc)"
cmake --install pfunit-build
