#!/usr/bin/env bash
set -euo pipefail

VERSION="${VERSION:?missing}"
ARCH="${ARCH:?missing}"
HAMLIB_BRANCH="${HAMLIB_BRANCH:?missing}"
WSJT_RELEASE_CHANNEL="${WSJT_RELEASE_CHANNEL:-DEVEL}"
WSJT_RC_NUMBER="${WSJT_RC_NUMBER:-}"
if [ "$ARCH" != armhf ]; then
  echo "build-linux-armhf-cross.sh supports only arch=armhf" >&2
  exit 2
fi

unset GITHUB_TOKEN
cd /work
# shellcheck source=.github/scripts/armhf-ci-image-config.sh
source .github/scripts/armhf-ci-image-config.sh
# shellcheck source=.github/scripts/linux-artifact-validation.sh
source .github/scripts/linux-artifact-validation.sh
.github/scripts/verify-armhf-ci-image.sh cross-builder "$ARCH" "$HAMLIB_BRANCH"

export CC="$ARMHF_TOOLCHAIN_PREFIX/bin/$ARMHF_TARGET_TRIPLET-gcc"
export CXX="$ARMHF_TOOLCHAIN_PREFIX/bin/$ARMHF_TARGET_TRIPLET-g++"
export FC="$ARMHF_TOOLCHAIN_PREFIX/bin/$ARMHF_TARGET_TRIPLET-gfortran"
for compiler in "$CC" "$CXX" "$FC"; do
  version="$("$compiler" -dumpfullversion -dumpversion)"
  if [ "$version" != "$ARMHF_GCC_VERSION" ]; then
    echo "Expected GCC $ARMHF_GCC_VERSION, found $version at $compiler" >&2
    exit 1
  fi
done

export CCACHE_DIR="${CCACHE_DIR:-/work/.ccache-armhf}"
mkdir -p "$CCACHE_DIR"
ccache --zero-stats
started="$(date +%s)"
cmake -S . -B wsjtx-build \
  -DCMAKE_TOOLCHAIN_FILE=/work/.github/cmake/armhf-toolchain.cmake \
  -DCMAKE_C_COMPILER_LAUNCHER=ccache \
  -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
  -DWSJT_SKIP_MANPAGES=ON \
  -DWSJT_ENABLE_TESTS=ON \
  -DWSJT_FORTRAN_LIBRARY_VARIANTS=OPENMP_ONLY \
  -DWSJT_RELEASE_CHANNEL="$WSJT_RELEASE_CHANNEL" \
  -DWSJT_RC_NUMBER="$WSJT_RC_NUMBER" \
  -Wno-dev
cmake --build wsjtx-build --parallel "$(nproc)"
ended="$(date +%s)"
{
  printf 'started_epoch=%s\n' "$started"
  printf 'ended_epoch=%s\n' "$ended"
  printf 'duration_seconds=%s\n' "$((ended - started))"
} > wsjtx-build/armhf-cross-build-timing.env
ccache --show-stats

validate_linux_build_executables wsjtx-build "$ARCH"
.github/scripts/audit-armhf-cross-build.sh wsjtx-build
echo "::group::Prepare ARMHF AppDir"
cmake --install wsjtx-build --prefix /work/AppDir/usr
validate_linux_application_tree AppDir appdir "$ARCH"
echo "::endgroup::"
echo "ARMHF cross-build complete for version=$VERSION"
