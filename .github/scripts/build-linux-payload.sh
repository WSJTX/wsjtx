#!/bin/bash
# build-linux-payload.sh — Build wsjtx for one Linux arch end-to-end.
#
# Used for the armhf build, which runs inside an arm/v7 container under
# QEMU user-mode emulation on an aarch64 host. The composite action at
# .github/actions/build-linux-payload/action.yml is source-of-truth for
# x86_64 + aarch64 (which use GHA cache primitives interleaved with build
# steps and run as composite-action `uses:` from build-linux.yml).
#
# Why a separate script for armhf: GHA's `container:` directive mounts
# the host's `/__e/node20/bin/node` (an aarch64 binary) into the
# container. Inside an arm/v7 container, the aarch64 dynamic loader
# `/lib/ld-linux-aarch64.so.1` is absent, so JS-based actions
# (setup-qemu-action, actions/checkout, actions/cache, upload-artifact,
# the composite action's `uses:` invocation itself) fail with
# `exec /__e/node20/bin/node: no such file or directory`. The fix is to
# run the JOB on the host (no `container:` directive) and explicitly
# `docker run --platform linux/arm/v7 ... bash <this script>` for the
# build payload. JS actions on the host work natively (host is aarch64,
# matches its node binary). Only the build payload itself runs inside
# the arm/v7 container, which is what we wanted.
#
# Required env vars (passed via docker run -e):
#   VERSION         — wsjtx version string (e.g. 3.0.1)
#   ARCH            — linux arch (armhf for the QEMU path)
#   HAMLIB_BRANCH   - hamlib branch (e.g. 4.7.2)
# Optional env vars:
#   WSJT_RELEASE_CHANNEL — DEVEL, RC, or GA
#   WSJT_RC_NUMBER       — release candidate number when channel is RC
#   GITHUB_TOKEN         — authenticates linuxdeploy-plugin-qt API lookups
#
# Required mount (passed via docker run -v):
#   /work           — the runner's $GITHUB_WORKSPACE bind-mounted into
#                     the container. Cache restores (pfunit-prefix,
#                     hamlib-prefix) land here BEFORE this script runs;
#                     produced artifacts (.deb, .rpm, .AppImage) land
#                     here for the host workflow to upload after this
#                     script returns.

set -euo pipefail

VERSION="${VERSION:?missing}"
ARCH="${ARCH:?missing}"
HAMLIB_BRANCH="${HAMLIB_BRANCH:?missing}"
WSJT_RELEASE_CHANNEL="${WSJT_RELEASE_CHANNEL:-DEVEL}"
WSJT_RC_NUMBER="${WSJT_RC_NUMBER:-}"

cd /work

# ── 1. Install build deps ────────────────────────────────────────────
# The Bookworm GCC image runs as root and already provides GCC, G++, and
# GFortran in /usr/local. Keep the remaining packages aligned with the
# composite action's dependency step.
apt-get update
apt-get install -y --no-install-recommends \
  ca-certificates curl git \
  build-essential cmake \
  libfftw3-dev libboost-all-dev \
  qtbase5-dev qttools5-dev qtmultimedia5-dev libqt5serialport5-dev \
  libqt5sql5-sqlite libqt5websockets5-dev \
  libqt5multimedia5-plugins \
  libusb-1.0-0-dev libudev-dev libreadline-dev \
  autoconf automake libtool pkg-config \
  texinfo \
  dpkg-dev fakeroot \
  asciidoctor \
  rpm \
  python3 \
  file xz-utils xauth xvfb \
  portaudio19-dev

export CC=/usr/local/bin/gcc
export CXX=/usr/local/bin/g++
export FC=/usr/local/bin/gfortran

for compiler in "$CC" "$CXX" "$FC"; do
  if [ ! -x "$compiler" ]; then
    echo "::error::Expected compiler not found at $compiler"
    exit 1
  fi
  version=$("$compiler" -dumpfullversion -dumpversion)
  echo "$compiler: $version"
  if [ "$version" != "13.4.0" ]; then
    echo "::error::Expected GCC 13.4.0, found $version at $compiler"
    exit 1
  fi
done

# ── 2. Build pFUnit if cache empty ───────────────────────────────────
# GHA actions/cache restored pfunit-prefix on the host before docker
# run; we detect cache hit by presence of PFUNITConfig.cmake.
if find pfunit-prefix -name PFUNITConfig.cmake -print -quit 2>/dev/null | grep -q .; then
  echo "pFUnit cache hit — skipping rebuild"
else
  echo "::group::Build pFUnit (cache miss)"
  rm -rf pfunit-src pfunit-build pfunit-prefix
  git clone --depth 1 --branch v4.14.0 --recursive \
    https://github.com/Goddard-Fortran-Ecosystem/pFUnit.git pfunit-src
  cmake -S pfunit-src -B pfunit-build \
    -DSKIP_MPI=YES \
    -DSKIP_OPENMP=YES \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    -DCMAKE_INSTALL_PREFIX="${PWD}/pfunit-prefix"
  cmake --build pfunit-build -j"$(nproc)"
  cmake --install pfunit-build
  echo "::endgroup::"
fi

PFUNIT_CONFIG=$(find pfunit-prefix -name PFUNITConfig.cmake -print -quit)
if [ -z "$PFUNIT_CONFIG" ]; then
  echo "::error::PFUNITConfig.cmake not found under pfunit-prefix"
  exit 1
fi
PFUNIT_DIR=$(dirname "$PFUNIT_CONFIG")
echo "pFUnit config dir: $PFUNIT_DIR"

# ── 3. Build Hamlib if cache empty ───────────────────────────────────
if [ -f hamlib-prefix/lib/libhamlib.a ]; then
  echo "Hamlib cache hit — skipping rebuild"
else
  echo "::group::Build Hamlib (cache miss)"
  rm -rf hamlib-src hamlib-prefix
  git clone --depth 1 --branch "$HAMLIB_BRANCH" \
    https://github.com/Hamlib/Hamlib.git hamlib-src
  (
    cd hamlib-src
    ./bootstrap
    ./configure \
      --prefix="${PWD}/../hamlib-prefix" \
      --disable-shared --enable-static \
      --without-cxx-binding \
      CFLAGS="-g -O2 -fPIC -fdata-sections -ffunction-sections" \
      LDFLAGS="-Wl,--gc-sections"
    make -j"$(nproc)"
    make install
  )
  echo "::endgroup::"
fi

# ── 4. Configure + build wsjtx ───────────────────────────────────────
echo "::group::wsjtx configure + build"
cmake -S . -B wsjtx-build \
  -DCMAKE_PREFIX_PATH="${PWD}/hamlib-prefix;${PWD}/pfunit-prefix" \
  -DWSJT_SKIP_MANPAGES=ON \
  -DWSJT_ENABLE_TESTS=ON \
  -DWSJT_FORTRAN_LIBRARY_VARIANTS=OPENMP_ONLY \
  -DWSJT_RELEASE_CHANNEL="${WSJT_RELEASE_CHANNEL}" \
  -DWSJT_RC_NUMBER="${WSJT_RC_NUMBER}" \
  -DPFUNIT_DIR="$PFUNIT_DIR" \
  -Wno-dev
cmake --build wsjtx-build -j"$(nproc)"
echo "::endgroup::"

# ── 5. Run tests ─────────────────────────────────────────────────────
echo "::group::wsjtx ctest"
(
  cd wsjtx-build
  QT_QPA_PLATFORM=xcb xvfb-run -a -s "-screen 0 1280x1024x24" \
    ctest --output-on-failure --output-junit ctest-results.xml
)
echo "::endgroup::"

# ── 6. Verify binary is the right arch ───────────────────────────────
# Defensive — catches a cross-arch leak where (e.g.) host toolchain
# slipped through QEMU. armhf must be 32-bit ARM EABI5 hard-float.
FILE_OUT=$(file wsjtx-build/jt9)
echo "$FILE_OUT"
case "$ARCH" in
  armhf)
    echo "$FILE_OUT" | grep -q "ELF 32-bit"
    echo "$FILE_OUT" | grep -q "ARM, EABI5"
    ;;
  *)
    echo "$FILE_OUT" | grep -q "ELF 64-bit"
    ;;
esac

# ── 7. Package .deb ──────────────────────────────────────────────────
echo "::group::Package .deb"
(
  cd wsjtx-build
  cpack -G DEB \
    -D "CPACK_DEBIAN_FILE_NAME=wsjtx-${VERSION}-linux-${ARCH}.deb"
)
DEB="wsjtx-build/wsjtx-${VERSION}-linux-${ARCH}.deb"
if [ ! -f "$DEB" ]; then
  echo "::error::cpack -G DEB produced no .deb at expected path: $DEB"
  ls -la wsjtx-build/*.deb 2>/dev/null || true
  exit 1
fi
ls -lh "$DEB"
file "$DEB"
# sed instead of `| head -20` so dpkg-deb doesn't get SIGPIPE'd by
# head's early close — same defensive pattern as the composite action
# (Learning #205, S137).
dpkg-deb --info "$DEB" | sed -n '1,20p'
echo "::endgroup::"

# ── 8. Package RPM ───────────────────────────────────────────────────
echo "::group::Package RPM"
(
  cd wsjtx-build
  cpack -G RPM
)
ls -lh wsjtx-build/*.rpm
RPM=$(ls wsjtx-build/*.rpm | sed -n '1p')
echo "Querying $RPM"
rpm -qpi "$RPM"
rpm -qpR "$RPM"
RPM_FILES=$(rpm -qpl "$RPM")
echo "$RPM_FILES" | sed -n '1,40p'
echo "$RPM_FILES" | grep -qE '/bin/wsjtx$'
echo "$RPM_FILES" | grep -qE '/bin/jt9$'
echo "$RPM_FILES" | grep -qE '/bin/qmap$'
echo "$RPM_FILES" | grep -qE '/bin/map65$'
echo "::endgroup::"

# ── 9. Install to AppDir for AppImage packaging ──────────────────────
echo "::group::Install to AppDir"
cmake --install wsjtx-build --prefix "${PWD}/AppDir/usr"
test -x AppDir/usr/bin/wsjtx
test -x AppDir/usr/bin/jt9
test -x AppDir/usr/bin/qmap
test -x AppDir/usr/bin/map65
test -x AppDir/usr/bin/ft8code
test -f AppDir/usr/share/applications/wsjtx.desktop
test -f AppDir/usr/share/pixmaps/wsjtx_icon.png
echo "::endgroup::"

# ── 10. Package AppImage ─────────────────────────────────────────────
echo "::group::Package AppImage"
LINUXDEPLOY_TAG="1-alpha-20251107-1"
curl_flags=(--fail --show-error --silent --location --retry 5 --retry-delay 5)
api_headers=(-H "User-Agent: wsjtx-ci")
if [ -n "${GITHUB_TOKEN:-}" ]; then
  api_headers+=(-H "Authorization: Bearer ${GITHUB_TOKEN}")
fi
case "$ARCH" in
  x86_64)
    LINUXDEPLOY_SHA256="c20cd71e3a4e3b80c3483cef793cda3f4e990aca14014d23c544ca3ce1270b4d"
    QT_PLUGIN_SHA256="be1b7e166bf9975cfb694ebe6759ba40502ffc6196440d3e64aa90c4dbd67e9f"
    ;;
  aarch64)
    LINUXDEPLOY_SHA256="620095110d693282b8ebeb244a95b5e911cf8f65f76c88b4b47d16ae6346fcff"
    QT_PLUGIN_SHA256="5525e6c49c3c774c02b8864d2acc2ae4c5c0ccc3327f7dce626deaa36348e5c6"
    ;;
  armhf)
    LINUXDEPLOY_SHA256="e359161979fa4bee50b92ce7102fb510299caebf34f711d983fba7a8f4bb1c2e"
    QT_PLUGIN_SHA256="463c3d853e78adc0bde8a9b2806c19b20eb60dd18799f8c6e68d873fb2be8ba0"
    ;;
  *)       echo "::error::Unknown linuxdeploy arch: $ARCH"; exit 1 ;;
esac

QT_PLUGIN_ASSET_NAME="linuxdeploy-plugin-qt-${ARCH}.AppImage"
QT_PLUGIN_ASSET_ID="$(
  curl "${curl_flags[@]}" "${api_headers[@]}" -H "Accept: application/vnd.github+json" \
    "https://api.github.com/repos/linuxdeploy/linuxdeploy-plugin-qt/releases/tags/continuous" |
  python3 -c 'import json, sys; name = sys.argv[1]; matches = [asset for asset in json.load(sys.stdin)["assets"] if asset["name"] == name]; print(matches[0]["id"]) if matches else sys.exit("asset not found: " + name)' "$QT_PLUGIN_ASSET_NAME"
)"

# If upstream refreshes the continuous Qt plugin assets, this lookup will keep
# finding the right asset ID by name; recalculate QT_PLUGIN_SHA256 after
# reviewing the new binary.
curl "${curl_flags[@]}" -o linuxdeploy.AppImage \
  "https://github.com/linuxdeploy/linuxdeploy/releases/download/${LINUXDEPLOY_TAG}/linuxdeploy-${ARCH}.AppImage"
curl "${curl_flags[@]}" "${api_headers[@]}" -H "Accept: application/octet-stream" -o linuxdeploy-plugin-qt.AppImage \
  "https://api.github.com/repos/linuxdeploy/linuxdeploy-plugin-qt/releases/assets/${QT_PLUGIN_ASSET_ID}"
echo "${LINUXDEPLOY_SHA256}  linuxdeploy.AppImage" | sha256sum -c -
echo "${QT_PLUGIN_SHA256}  linuxdeploy-plugin-qt.AppImage" | sha256sum -c -
chmod +x linuxdeploy.AppImage linuxdeploy-plugin-qt.AppImage

# APPIMAGE_EXTRACT_AND_RUN=1 extends FUSE-less behavior to every child
# AppImage in the linuxdeploy invocation tree (qt plugin + appimagetool,
# both invoked as AppImages internally). Same defense as the composite
# action's Bookworm container leg (Learning #206, S137).
export APPIMAGE_EXTRACT_AND_RUN=1
export OUTPUT="wsjtx-${VERSION}-linux-${ARCH}.AppImage"
./linuxdeploy.AppImage --appimage-extract-and-run \
  --appdir AppDir \
  --plugin qt \
  --output appimage \
  --desktop-file AppDir/usr/share/applications/wsjtx.desktop \
  --icon-file AppDir/usr/share/pixmaps/wsjtx_icon.png

ls -lh "$OUTPUT"
file "$OUTPUT"
echo "::endgroup::"

# ── 11. Smoke-test AppImage payload ──────────────────────────────────
echo "::group::Smoke-test AppImage payload"
./"$OUTPUT" --appimage-extract >/dev/null
test -x squashfs-root/usr/bin/wsjtx
test -x squashfs-root/usr/bin/jt9
test -x squashfs-root/usr/bin/qmap
test -x squashfs-root/usr/bin/map65
test -x squashfs-root/usr/bin/ft8code
test -x squashfs-root/usr/bin/wsprd
FILE_OUT=$(file squashfs-root/usr/bin/wsjtx)
case "$ARCH" in
  armhf)
    echo "$FILE_OUT" | grep -q "ELF 32-bit"
    echo "$FILE_OUT" | grep -q "ARM, EABI5"
    ;;
  *)
    echo "$FILE_OUT" | grep -q "ELF 64-bit"
    ;;
esac
LIBS=$(ls squashfs-root/usr/lib/)
echo "$LIBS" | grep -q "^libQt5Core\.so"
echo "$LIBS" | grep -q "^libQt5Widgets\.so"
echo "$LIBS" | grep -q "^libQt5Multimedia\.so"
audio_plugins=$(find squashfs-root -path '*/plugins/audio/*.so' 2>/dev/null || true)
if [ -z "$audio_plugins" ]; then
  echo "::error::No Qt audio backend plugins (qtaudio_alsa/qtaudio_pulse) bundled in AppImage."
  echo "::error::Install libqt5multimedia5-plugins on the build host so linuxdeploy-plugin-qt can find them."
  echo "AppImage plugin tree:"
  find squashfs-root -path '*/plugins/*' -name '*.so' 2>/dev/null | sed -n '1,20p'
  exit 1
fi
echo "Qt audio plugins bundled:"
echo "$audio_plugins"
rm -rf squashfs-root
echo "::endgroup::"

echo "Build payload complete for arch=${ARCH}, version=${VERSION}"
