#!/usr/bin/env bash
set -euo pipefail

VERSION="${VERSION:?missing}"
ARCH="${ARCH:?missing}"
if [ "$ARCH" != armhf ]; then
  echo "package-linux-armhf.sh supports only arch=armhf" >&2
  exit 2
fi

github_api_token="${GITHUB_TOKEN:-}"
unset GITHUB_TOKEN
cd /work
# shellcheck source=.github/scripts/armhf-ci-image-config.sh
source .github/scripts/armhf-ci-image-config.sh
# shellcheck source=.github/scripts/linux-artifact-validation.sh
source .github/scripts/linux-artifact-validation.sh
TARGET="linux-${ARCH}-${ARMHF_DISTRO:-bookworm}"
export LD_LIBRARY_PATH="$ARMHF_RUNTIME_PREFIX"

echo "::group::Package .deb"
(
  cd wsjtx-build
  cpack -G DEB \
    -D "CPACK_DEBIAN_FILE_NAME=wsjtx-${VERSION}-${TARGET}.deb" \
    -D "CPACK_INSTALL_CMAKE_PROJECTS=" \
    -D "CPACK_INSTALLED_DIRECTORIES=/work/AppDir;/"
)
DEB="wsjtx-build/wsjtx-${VERSION}-${TARGET}.deb"
if [ ! -f "$DEB" ]; then
  echo "::error::cpack -G DEB produced no package at $DEB"
  exit 1
fi
dpkg-deb --info "$DEB" | sed -n '1,20p'
deb_arch="$(dpkg-deb -f "$DEB" Architecture)"
if [ "$deb_arch" != armhf ]; then
  echo "::error::DEB architecture is $deb_arch, expected armhf"
  exit 1
fi
validate_deb_package "$DEB"
echo "::endgroup::"

echo "::group::Package RPM"
(
  cd wsjtx-build
  cpack -G RPM \
    -D "CPACK_RPM_FILE_NAME=wsjtx-${VERSION}-${TARGET}.rpm" \
    -D "CPACK_INSTALL_CMAKE_PROJECTS=" \
    -D "CPACK_INSTALLED_DIRECTORIES=/work/AppDir;/"
)
RPM="wsjtx-build/wsjtx-${VERSION}-${TARGET}.rpm"
if [ ! -f "$RPM" ]; then
  echo "::error::cpack -G RPM produced no package"
  exit 1
fi
rpm_arch="$(rpm -qp --qf '%{ARCH}' "$RPM")"
case "$rpm_arch" in
  armv7hl|armv7l) ;;
  *) echo "::error::RPM architecture is $rpm_arch, expected armv7"; exit 1 ;;
esac
rpm -qpi "$RPM"
rpm -qpR "$RPM"
validate_rpm_package "$RPM"
echo "::endgroup::"

echo "::group::Validate AppDir"
validate_linux_application_tree AppDir appdir "$ARCH"
validate_armhf_elf_tree AppDir "${ARMHF_DISTRO:-bookworm}"
echo "::endgroup::"

echo "::group::Package AppImage"
LINUXDEPLOY_TAG=1-alpha-20251107-1
LINUXDEPLOY_SHA256=e359161979fa4bee50b92ce7102fb510299caebf34f711d983fba7a8f4bb1c2e
QT_PLUGIN_ASSET_NAME=linuxdeploy-plugin-qt-armhf.AppImage
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
QT_PLUGIN_ASSET_ID="$(
  github_api_curl -H "Accept: application/vnd.github+json" \
    "https://api.github.com/repos/linuxdeploy/linuxdeploy-plugin-qt/releases/tags/continuous" |
  python3 -c 'import json, sys; name = sys.argv[1]; matches = [asset for asset in json.load(sys.stdin)["assets"] if asset["name"] == name]; print(matches[0]["id"]) if matches else sys.exit("asset not found: " + name)' "$QT_PLUGIN_ASSET_NAME"
)"
curl "${curl_flags[@]}" -o linuxdeploy.AppImage \
  "https://github.com/linuxdeploy/linuxdeploy/releases/download/${LINUXDEPLOY_TAG}/linuxdeploy-armhf.AppImage"
github_api_curl -H "Accept: application/octet-stream" -o linuxdeploy-plugin-qt.AppImage \
  "https://api.github.com/repos/linuxdeploy/linuxdeploy-plugin-qt/releases/assets/${QT_PLUGIN_ASSET_ID}"
echo "$LINUXDEPLOY_SHA256  linuxdeploy.AppImage" | sha256sum -c -
chmod +x linuxdeploy.AppImage linuxdeploy-plugin-qt.AppImage
# linuxdeploy resolves dependencies with the non-executing ARMHF resolver,
# which reads the clean runtime prefix first and refuses a libquadmath
# dependency the ARMHF runtime cannot satisfy.
mkdir -p wsjtx-build/appimage-tools
ln -sfn /work/.github/scripts/armhf-static-ldd.sh wsjtx-build/appimage-tools/ldd

for target in AppDir/usr/bin/wsjtx AppDir/usr/bin/jt9 AppDir/usr/bin/qmap \
  AppDir/usr/bin/map65; do
  wsjtx-build/appimage-tools/ldd "$target"
done

export APPIMAGE_EXTRACT_AND_RUN=1
export OUTPUT="wsjtx-${VERSION}-${TARGET}.AppImage"
env -u GITHUB_TOKEN PATH="/work/wsjtx-build/appimage-tools:$PATH" \
  ./linuxdeploy.AppImage --appimage-extract-and-run \
  --appdir AppDir \
  --plugin qt \
  --output appimage \
  --desktop-file AppDir/usr/share/applications/wsjtx.desktop \
  --icon-file AppDir/usr/share/pixmaps/wsjtx_icon.png
file "$OUTPUT"
echo "::endgroup::"

echo "::group::Validate AppImage payload"
./"$OUTPUT" --appimage-extract >/dev/null
validate_linux_application_tree squashfs-root appimage "$ARCH"
validate_armhf_elf_tree squashfs-root "${ARMHF_DISTRO:-bookworm}"
rm -rf /work/squashfs-root
echo "::endgroup::"

echo "::group::AppImage startup smoke"
run_packaged_appimage_startup_smoke \
  "./$OUTPUT" wsjtx-build/appimage-startup-smoke.log
echo "::endgroup::"

echo "::group::Package release tarballs"
PATH="/work/wsjtx-build/appimage-tools:$PATH" .github/scripts/package-cli-tools.sh \
  --platform linux \
  --version "$VERSION" \
  --arch "$ARCH" \
  --distro "${ARMHF_DISTRO:-bookworm}" \
  --groups CMake/release-tarballs.txt \
  --source wsjtx-build \
  --linuxdeploy /work/linuxdeploy.AppImage \
  --out release-tarballs
echo "::endgroup::"

# LD_LIBRARY_PATH, exported above, would hide a GCC runtime library missing
# from a tarball; the smoke test unsets it for every program it runs.
echo "::group::Smoke-test release tarballs"
.github/scripts/smoke-release-tarballs.sh \
  --platform linux \
  --version "$VERSION" \
  --target "$TARGET" \
  --groups CMake/release-tarballs.txt \
  --dir release-tarballs
echo "::endgroup::"

echo "ARMHF package phase complete for version=$VERSION target=$TARGET"
