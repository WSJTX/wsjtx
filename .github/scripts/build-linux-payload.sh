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
#   CCACHE_DIR           — bind-mounted compiler cache directory
#
# Required mount (passed via docker run -v):
#   /work           — the runner's $GITHUB_WORKSPACE bind-mounted into
#                     the container. Produced artifacts land here for the
#                     host workflow to upload after this script returns.

set -euo pipefail

# Keep the token out of build, test, and packaging subprocess environments. The
# authenticated curl helper below reintroduces it only for its curl process.
github_api_token="${GITHUB_TOKEN:-}"
unset GITHUB_TOKEN

VERSION="${VERSION:?missing}"
ARCH="${ARCH:?missing}"
HAMLIB_BRANCH="${HAMLIB_BRANCH:?missing}"
WSJT_RELEASE_CHANNEL="${WSJT_RELEASE_CHANNEL:-DEVEL}"
WSJT_RC_NUMBER="${WSJT_RC_NUMBER:-}"

cd /work
# shellcheck source=.github/scripts/linux-artifact-validation.sh
source .github/scripts/linux-artifact-validation.sh

# ── 1. Verify the baked dependency environment ──────────────────────
.github/scripts/verify-linux-ci-image.sh normal "$ARCH" "$HAMLIB_BRANCH"

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

PFUNIT_CONFIG=$(find /opt/wsjtx/pfunit -name PFUNITConfig.cmake -print -quit)
if [ -z "$PFUNIT_CONFIG" ]; then
  echo "::error::PFUNITConfig.cmake not found in the Linux CI image"
  exit 1
fi
PFUNIT_DIR=$(dirname "$PFUNIT_CONFIG")
echo "pFUnit config dir: $PFUNIT_DIR"

export CCACHE_DIR="${CCACHE_DIR:-/work/.ccache-armhf}"
mkdir -p "$CCACHE_DIR"
ccache --show-config
ccache --zero-stats

# ── 2. Configure + build wsjtx ───────────────────────────────────────
echo "::group::wsjtx configure + build"
cmake -S . -B wsjtx-build \
  -DCMAKE_PREFIX_PATH="/opt/wsjtx/hamlib;/opt/wsjtx/pfunit" \
  -DCMAKE_C_COMPILER_LAUNCHER=ccache \
  -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
  -DWSJT_SKIP_MANPAGES=ON \
  -DWSJT_ENABLE_TESTS=ON \
  -DWSJT_FORTRAN_LIBRARY_VARIANTS=OPENMP_ONLY \
  -DWSJT_RELEASE_CHANNEL="${WSJT_RELEASE_CHANNEL}" \
  -DWSJT_RC_NUMBER="${WSJT_RC_NUMBER}" \
  -DPFUNIT_DIR="$PFUNIT_DIR" \
  -Wno-dev
cmake --build wsjtx-build -j"$(nproc)"
ccache --show-stats
echo "::endgroup::"

# ── 3. Run tests ─────────────────────────────────────────────────────
echo "::group::wsjtx ctest"
(
  cd wsjtx-build
  QT_QPA_PLATFORM=xcb xvfb-run -a -s "-screen 0 1280x1024x24" \
    ctest --output-on-failure --output-junit ctest-results.xml
)
echo "::endgroup::"

# ── 4. Verify built executables ──────────────────────────────────────
validate_linux_build_executables wsjtx-build "$ARCH"

# ── 5. Package .deb ──────────────────────────────────────────────────
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
validate_deb_package "$DEB"
echo "::endgroup::"

# ── 6. Package RPM ───────────────────────────────────────────────────
echo "::group::Package RPM"
(
  cd wsjtx-build
  cpack -G RPM
)
ls -lh wsjtx-build/*.rpm
rpms=(wsjtx-build/*.rpm)
RPM=${rpms[0]}
echo "Querying $RPM"
rpm -qpi "$RPM"
rpm -qpR "$RPM"
validate_rpm_package "$RPM"
echo "::endgroup::"

# ── 7. Install to AppDir for AppImage packaging ──────────────────────
echo "::group::Install to AppDir"
cmake --install wsjtx-build --prefix "${PWD}/AppDir/usr"
validate_linux_application_tree AppDir appdir "$ARCH"
echo "::endgroup::"

# ── 8. Package AppImage ──────────────────────────────────────────────
echo "::group::Package AppImage"
LINUXDEPLOY_TAG="1-alpha-20251107-1"
curl_flags=(--fail --show-error --silent --location --retry 5 --retry-delay 5)
github_api_curl() {
  local api_headers=(-H "User-Agent: wsjtx-ci")
  if [ -n "$github_api_token" ]; then
    api_headers+=(-H "Authorization: Bearer ${github_api_token}")
    env GITHUB_TOKEN="$github_api_token" curl "${curl_flags[@]}" "${api_headers[@]}" "$@"
  else
    curl "${curl_flags[@]}" "${api_headers[@]}" "$@"
  fi
}
case "$ARCH" in
  x86_64)
    LINUXDEPLOY_SHA256="c20cd71e3a4e3b80c3483cef793cda3f4e990aca14014d23c544ca3ce1270b4d"
    ;;
  aarch64)
    LINUXDEPLOY_SHA256="620095110d693282b8ebeb244a95b5e911cf8f65f76c88b4b47d16ae6346fcff"
    ;;
  armhf)
    LINUXDEPLOY_SHA256="e359161979fa4bee50b92ce7102fb510299caebf34f711d983fba7a8f4bb1c2e"
    ;;
  *)       echo "::error::Unknown linuxdeploy arch: $ARCH"; exit 1 ;;
esac

QT_PLUGIN_ASSET_NAME="linuxdeploy-plugin-qt-${ARCH}.AppImage"
QT_PLUGIN_ASSET_ID="$(
  github_api_curl -H "Accept: application/vnd.github+json" \
    "https://api.github.com/repos/linuxdeploy/linuxdeploy-plugin-qt/releases/tags/continuous" |
  python3 -c 'import json, sys; name = sys.argv[1]; matches = [asset for asset in json.load(sys.stdin)["assets"] if asset["name"] == name]; print(matches[0]["id"]) if matches else sys.exit("asset not found: " + name)' "$QT_PLUGIN_ASSET_NAME"
)"

# Resolve the current asset ID because the Qt plugin intentionally follows
# upstream's rolling continuous release.
curl "${curl_flags[@]}" -o linuxdeploy.AppImage \
  "https://github.com/linuxdeploy/linuxdeploy/releases/download/${LINUXDEPLOY_TAG}/linuxdeploy-${ARCH}.AppImage"
github_api_curl -H "Accept: application/octet-stream" -o linuxdeploy-plugin-qt.AppImage \
  "https://api.github.com/repos/linuxdeploy/linuxdeploy-plugin-qt/releases/assets/${QT_PLUGIN_ASSET_ID}"
echo "${LINUXDEPLOY_SHA256}  linuxdeploy.AppImage" | sha256sum -c -
chmod +x linuxdeploy.AppImage linuxdeploy-plugin-qt.AppImage

# APPIMAGE_EXTRACT_AND_RUN=1 extends FUSE-less behavior to every child
# AppImage in the linuxdeploy invocation tree (qt plugin + appimagetool,
# both invoked as AppImages internally). Same defense as the composite
# action's Bookworm container leg (Learning #206, S137).
export APPIMAGE_EXTRACT_AND_RUN=1
export OUTPUT="wsjtx-${VERSION}-linux-${ARCH}.AppImage"
env -u GITHUB_TOKEN ./linuxdeploy.AppImage --appimage-extract-and-run \
  --appdir AppDir \
  --plugin qt \
  --output appimage \
  --desktop-file AppDir/usr/share/applications/wsjtx.desktop \
  --icon-file AppDir/usr/share/pixmaps/wsjtx_icon.png

ls -lh "$OUTPUT"
file "$OUTPUT"
echo "::endgroup::"

# ── 9. Validate AppImage payload ─────────────────────────────────────
echo "::group::Smoke-test AppImage payload"
./"$OUTPUT" --appimage-extract >/dev/null
validate_linux_application_tree squashfs-root appimage "$ARCH"
rm -rf squashfs-root
echo "::endgroup::"

# ── 10. Run the packaged AppImage ────────────────────────────────────
echo "::group::AppImage startup smoke"
run_packaged_appimage_startup_smoke \
  "./$OUTPUT" \
  wsjtx-build/appimage-startup-smoke.log
echo "::endgroup::"

echo "Build payload complete for arch=${ARCH}, version=${VERSION}"
