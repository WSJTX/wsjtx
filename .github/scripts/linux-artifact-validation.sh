#!/bin/bash
# Shared acceptance checks for Linux build products.

linux_validation_error() {
  echo "::error::$*" >&2
  return 1
}

validate_linux_boost_patch() {
  local script_dir
  script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
  python3 - "$1" "$2" "$script_dir/patch-boost-log-queue.py" <<'PY'
import hashlib
import importlib.util
import json
from pathlib import Path
import sys

root = Path(sys.argv[1]).resolve()
version, helper = sys.argv[2:]
try:
    spec = importlib.util.spec_from_file_location('boost_queue_patch', helper)
    patch = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(patch)
    manifest = json.loads((root / 'share/wsjtx/boost-log-queue-patch.json').read_text())
    expected = patch.SOURCE_HASHES
    patched = patch.PATCHED_HASHES
    assert version in patch.SUPPORTED.values(), 'unsupported Boost version'
    assert manifest['boost_version'] == version, 'patch version mismatch'
    assert manifest['patch'] == 'boost-log-queue-release-acquire-v1', 'queue patch missing'
    assert manifest['patch_helper_sha256'] == patch.digest(Path(helper).read_bytes()), 'stale queue patch recipe'
    assert set(manifest['files']) == set(expected), 'queue patch source manifest mismatch'
    for name, original in expected.items():
        entry = manifest['files'][name]
        assert entry['original_sha256'] == original, f'unrecognized queue source: {name}'
        assert entry['patched_sha256'] == patched[name], f'queue patch hash mismatch: {name}'
    header = root / 'include' / patch.HEADER
    assert root in header.resolve().parents, 'queue header escapes Boost prefix'
    assert hashlib.sha256(header.read_bytes()).hexdigest() == manifest['files'][patch.HEADER]['patched_sha256'], 'installed queue header differs from patched source'
except (OSError, ValueError, KeyError, AssertionError) as error:
    raise SystemExit(f'Boost queue provenance validation failed: {error}')
print(f'Validated Boost {version} queue patch: {root}')
PY
}

validate_linux_boost_archives() {
  local root="$1" arch="$2" version="$3" component archive headers machine expected script_dir members
  local readelf_command="${READELF:-readelf}"
  local ar_command="${AR:-ar}"
  script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
  # shellcheck source=.github/scripts/linux-boost-config.sh
  . "$script_dir/linux-boost-config.sh"
  linux_boost_source_config "$version" || return
  validate_linux_boost_patch "$root" "$version" || return
  python3 - "$root" "$version" "$arch" "$LINUX_BOOST_SOURCE_URL" \
    "$LINUX_BOOST_SOURCE_SHA256" "$(linux_boost_recipe_hash)" <<'PY' || return
import hashlib
import json
from pathlib import Path
import re
import sys

root = Path(sys.argv[1]).resolve()
version, arch, url, source_hash, recipe_hash = sys.argv[2:]
try:
    manifest = json.loads((root / 'share/wsjtx/boost-build.json').read_text())
    for key, expected in {'version': version, 'source_url': url, 'source_sha256': source_hash,
                          'recipe_sha256': recipe_hash, 'link': 'static', 'runtime_link': 'shared',
                          'threading': 'multi', 'patch_manifest': 'boost-log-queue-patch.json'}.items():
        assert manifest[key] == expected, f'{key} mismatch (rebuild Boost in a fresh prefix)'
    targets = {'armhf': r'arm.*-linux-gnueabihf', 'aarch64': r'aarch64.*-linux-gnu', 'x86_64': r'x86_64.*-linux-gnu'}
    assert re.fullmatch(targets[arch], manifest['compiler']['target']), 'compiler target mismatch'
    assert manifest['compiler']['path'] and manifest['compiler']['version'], 'compiler provenance missing'
    header = root / 'include/boost/version.hpp'
    assert root in header.resolve().parents, 'version header escapes Boost prefix'
    assert hashlib.sha256(header.read_bytes()).hexdigest() == manifest['version_header_sha256'], 'version header hash mismatch'
    major, minor, patch = map(int, version.split('.'))
    assert re.search(r'^#define BOOST_VERSION ' + str(major * 100000 + minor * 100 + patch) + r'\s*$', header.read_text(), re.MULTILINE), 'installed Boost version mismatch'
    archives = {path.name: path for path in (root / 'lib').glob('libboost_*.a')}
    assert archives and set(archives) == set(manifest['archives']), 'archive manifest differs from installed archives'
    assert not list((root / 'lib').glob('libboost_*.so*')), 'shared Boost libraries in static prefix'
    for name, path in archives.items():
        assert root in path.resolve().parents, f'archive escapes Boost prefix: {path}'
        assert path.read_bytes()[:8] == b'!<arch>\n', f'not a genuine static archive: {path}'
        assert hashlib.sha256(path.read_bytes()).hexdigest() == manifest['archives'][name], f'archive hash mismatch: {path}'
except (OSError, ValueError, KeyError, AssertionError) as error:
    raise SystemExit(f'Boost build provenance validation failed: {error}')
print(json.dumps(manifest, indent=2, sort_keys=True))
PY
  case "$arch" in
    armhf) expected=ARM ;;
    aarch64) expected=AArch64 ;;
    x86_64) expected='Advanced Micro Devices X86-64' ;;
    *) linux_validation_error "Unsupported Boost archive architecture: $arch"; return 2 ;;
  esac
  for component in log_setup log filesystem thread atomic chrono container; do
    archive="$root/lib/libboost_$component.a"
    if [ ! -f "$archive" ]; then
      linux_validation_error "Missing genuine Boost static archive: $archive"
      return 1
    fi
  done
  for archive in "$root"/lib/libboost_*.a; do
    if [ ! -f "$archive" ] || [ "$(head -c 7 "$archive")" != '!<arch>' ]; then
      linux_validation_error "Missing genuine Boost static archive: $archive"
      return 1
    fi
    members=$("$ar_command" t "$archive") || return
    if [ -z "$members" ]; then
      linux_validation_error "Required Boost archive has no object members: $archive"
      return 1
    fi
    headers=$("$readelf_command" -hW "$archive") || return
    machine=$(printf '%s\n' "$headers" | awk '/Machine:/ { sub(/^.*Machine:[[:space:]]*/, ""); print }' | sort -u)
    if [ "$machine" != "$expected" ]; then
      linux_validation_error "$archive has unexpected object architectures: $machine (expected $expected)"
      return 1
    fi
    sha256sum "$archive" || return
  done
}

validate_linux_boost_selection() {
  local script_dir
  script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
  # shellcheck source=.github/scripts/boost-version.sh
  . "$script_dir/boost-version.sh"
  python3 - "$1" "$2" "$BOOST_VERSION" <<'PY'
from pathlib import Path
import re
import shlex
import sys

build, root = map(lambda value: Path(value).resolve(), sys.argv[1:3])
version = sys.argv[3]
try:
    cache = {}
    for line in (build / 'CMakeCache.txt').read_text().splitlines():
        match = re.match(r'([^:#=]+):[^=]+=(.*)', line)
        if match:
            cache[match[1]] = match[2]
    enabled = ('ON', 'TRUE', '1')
    assert cache.get('Boost_USE_STATIC_LIBS', '').upper() in enabled, 'Boost_USE_STATIC_LIBS must be enabled'
    assert cache.get('Boost_NO_BOOST_CMAKE', '').upper() not in enabled, 'Boost config mode must be enabled'
    required_configs = {'Boost_DIR', 'boost_headers_DIR', 'boost_log_DIR', 'boost_log_setup_DIR'}
    assert required_configs <= cache.keys(), 'Boost config package locations missing from CMake cache'
    for key, value in cache.items():
        if key == 'Boost_DIR' or re.fullmatch(r'boost_[a-z0-9_]+_DIR', key):
            expected = root / 'lib/cmake' / (key.removesuffix('_DIR') + '-' + version)
            path = Path(value).resolve()
            assert path == expected and path.is_dir(), f'Boost config outside pinned prefix: {key}={value}'
    if 'Boost_INCLUDE_DIR' in cache:
        assert Path(cache['Boost_INCLUDE_DIR']).resolve() == root / 'include', 'Boost headers outside pinned prefix'
    includes = set()
    for path in build.rglob('flags.make'):
        for line in path.read_text().splitlines():
            if not line.startswith('CXX_INCLUDES ='):
                continue
            tokens = iter(shlex.split(line.split('=', 1)[1]))
            for token in tokens:
                if token in ('-I', '-isystem'):
                    includes.add(Path(next(tokens)).resolve())
                elif token.startswith('-I'):
                    includes.add(Path(token[2:]).resolve())
    assert root / 'include' in includes, 'generated compile inputs omit pinned Boost headers'
    def check_archive(value):
        path = Path(value)
        assert path.is_absolute() and root in path.resolve().parents, f'Boost link input outside pinned prefix: {value}'
        assert path.resolve().parent == root / 'lib', f'Boost link input outside audited library directory: {value}'
        assert path.name.startswith('libboost_') and path.suffix == '.a', f'non-static Boost link input: {value}'
        assert path.read_bytes()[:8] == b'!<arch>\n', f'invalid Boost archive: {value}'
    for key, value in cache.items():
        if re.fullmatch(r'Boost_[A-Z0-9_]+_LIBRARY_(RELEASE|DEBUG)', key) and not value.endswith('-NOTFOUND'):
            check_archive(value)
    linked = set()
    for path in build.rglob('link.txt'):
        for token in shlex.split(path.read_text()):
            if 'libboost_' in token or token.startswith('-lboost_'):
                check_archive(token)
                linked.add(Path(token).name)
    assert {'libboost_log.a', 'libboost_log_setup.a'} <= linked, 'generated link inputs omit pinned Boost logging archives'
except (OSError, ValueError, AssertionError, StopIteration) as error:
    raise SystemExit(f'Boost CMake selection validation failed: {error}')
print(f'Validated CMake config and linker Boost inputs: {root}')
PY
}

validate_linux_boost_dynamic() {
  local label="$1" dynamic="$2"
  if printf '%s\n' "$dynamic" | awk '/\(NEEDED\)/ && /\[libboost_/ { found=1 } END { exit !found }'; then
    linux_validation_error "$label imports a Boost shared library"
    return 1
  fi
}

validate_linux_boost_dependencies() {
  local label="$1" dependencies="$2"
  if [[ "$(printf '%s' "$dependencies" | tr '[:upper:]' '[:lower:]')" == *boost* ]]; then
    linux_validation_error "$label declares a Boost runtime dependency: $dependencies"
    return 1
  fi
}

validate_linux_static_boost_tree() {
  local root="$1" path metadata dynamic versions
  local readelf_command="${READELF:-readelf}"
  while IFS= read -r -d '' path; do
    case "${path##*/}" in
      libboost_*) linux_validation_error "Unexpected bundled Boost library: $path"; return 1 ;;
    esac
    [ -f "$path" ] || continue
    metadata=$(file -- "$path") || return
    [[ "$metadata" == *ELF* && "$metadata" != *relocatable* ]] || continue
    dynamic=$("$readelf_command" -dW -- "$path") || return
    validate_linux_boost_dynamic "$path" "$dynamic" || return
    versions=$("$readelf_command" -VW -- "$path") || return
    printf 'Runtime symbol requirements: %s\n' "$path"
    printf '%s\n' "$versions" | awk '
      /^Version needs section/ { needs = 1; next }
      /^Version (definition|symbols) section/ { needs = 0 }
      needs && /Name: (GLIBC|GLIBCXX|CXXABI|GFORTRAN|GOMP)_/ { print }
    '
  done < <(find "$root" \( -type f -o -type l \) -print0)
}

validate_elf_metadata() {
  local arch="$1"
  local path="$2"
  local file_output="$3"
  local header_output="$4"
  local program_output="$5"

  case "$arch" in
    armhf)
      [[ "$file_output" == *"ELF 32-bit"* ]] || {
        linux_validation_error "$path is not ELF32"
        return 1
      }
      [[ "$file_output" == *"ARM, EABI5"* ]] || {
        linux_validation_error "$path is not ARM EABI5"
        return 1
      }
      [[ "$header_output" == *"Class:"*"ELF32"* ]] || {
        linux_validation_error "$path has the wrong ELF class for armhf"
        return 1
      }
      [[ "$header_output" == *"Machine:"*"ARM"* ]] || {
        linux_validation_error "$path has the wrong ELF machine for armhf"
        return 1
      }
      [[ "$header_output" == *"Version5 EABI"* ]] || {
        linux_validation_error "$path does not use EABI5"
        return 1
      }
      [[ "$header_output" == *"hard-float ABI"* ]] || {
        linux_validation_error "$path does not use the ARM hard-float ABI"
        return 1
      }
      if [[ "$file_output" == *"executable"*"dynamically linked"* ]] &&
        [[ "$program_output" != *"/lib/ld-linux-armhf.so.3"* ]]; then
        linux_validation_error "$path does not request /lib/ld-linux-armhf.so.3"
        return 1
      fi
      ;;
    x86_64)
      [[ "$file_output" == *"ELF 64-bit"* && "$file_output" == *"x86-64"* ]] || {
        linux_validation_error "$path is not a 64-bit x86-64 ELF"
        return 1
      }
      ;;
    aarch64)
      [[ "$file_output" == *"ELF 64-bit"* && "$file_output" == *"ARM aarch64"* ]] || {
        linux_validation_error "$path is not a 64-bit AArch64 ELF"
        return 1
      }
      ;;
    *)
      linux_validation_error "Unsupported Linux architecture: $arch"
      return 2
      ;;
  esac
}

validate_elf_executable() {
  local arch="$1"
  local path="$2"
  local file_output
  local header_output
  local program_output
  local dynamic_output

  if [ ! -x "$path" ]; then
    linux_validation_error "Required executable is missing: $path"
    return 1
  fi

  file_output=$(file -- "$path") || return
  header_output=$(readelf -hW -- "$path") || return
  program_output=$(readelf -lW -- "$path") || return
  echo "$file_output"
  validate_elf_metadata "$arch" "$path" "$file_output" "$header_output" "$program_output" || return
  dynamic_output=$(readelf -dW -- "$path") || return
  validate_linux_boost_dynamic "$path" "$dynamic_output" || return
  if [ "${path##*/}" = jt9 ]; then
    if printf '%s\n' "$dynamic_output" | awk '/\(NEEDED\)/ && /\[libQt/ { found=1 } END { exit found ? 0 : 1 }'; then
      linux_validation_error "$path must not depend on Qt"
      return 1
    fi
  fi
}

validate_armhf_elf_tree() {
  local root="$1" distro="$2" ceiling path metadata header program dynamic versions version
  local readelf_command="${READELF:-readelf}" elf_count=0
  case "$distro" in
    bookworm) ceiling=2.36 ;;
    trixie) ceiling=2.41 ;;
    *) linux_validation_error "Unknown ARMHF distro: $distro"; return 2 ;;
  esac
  validate_linux_static_boost_tree "$root" || return
  while IFS= read -r -d '' path; do
    metadata=$(file -- "$path") || return
    [[ "$metadata" == *ELF* ]] || continue
    [[ "$metadata" != *relocatable* ]] || continue
    header=$("$readelf_command" -hW -- "$path") || return
    program=$("$readelf_command" -lW -- "$path") || return
    validate_elf_metadata armhf "$path" "$metadata" "$header" "$program" || return
    dynamic=$("$readelf_command" -dW -- "$path") || return
    if [[ "$dynamic" == *"Shared library: [libquadmath.so"* ]]; then
      linux_validation_error "$path unexpectedly depends on libquadmath"
      return 1
    fi
    versions=$("$readelf_command" -VW -- "$path" | awk '
      /^Version needs section/ { needs = 1; next }
      /^Version (definition|symbols) section/ { needs = 0 }
      needs { print }
    ') || return
    if [[ "$versions" == *GLIBC_PRIVATE* ]]; then
      linux_validation_error "$path requires a private glibc symbol"
      return 1
    fi
    while [[ "$versions" =~ GLIBC_([0-9]+\.[0-9]+(\.[0-9]+)?) ]]; do
      version=${BASH_REMATCH[1]}
      versions=${versions#*"${BASH_REMATCH[0]}"}
      if dpkg --compare-versions "$version" gt "$ceiling"; then
        linux_validation_error "$path requires GLIBC_$version, above $distro ceiling $ceiling"
        return 1
      fi
    done
    elf_count=$((elf_count + 1))
  done < <(find "$root" -type f -print0)
  if [ "$elf_count" -eq 0 ]; then
    linux_validation_error "No ELF executables or shared libraries found below $root"
    return 1
  fi
}

validate_linux_build_executables() {
  local build_dir="$1"
  local arch="$2"
  local executable

  for executable in wsjtx jt9 qmap/qmap map65/map65 ft8code wsprd; do
    validate_elf_executable "$arch" "$build_dir/$executable" || return
  done
}

linux_manifest_has_path() {
  local manifest="$1"
  local path="$2"
  case $'\n'"$manifest"$'\n' in
    *"$path"$'\n'*) return 0 ;;
    *) return 1 ;;
  esac
}

validate_linux_package_manifest() {
  local manifest="$1"
  local label="$2"
  local path

  for path in \
    /usr/bin/wsjtx \
    /usr/bin/jt9 \
    /usr/bin/qmap \
    /usr/bin/map65 \
    /usr/share/wsjtx/ALLCALL7.TXT \
    /usr/share/wsjtx/CALL3.TXT \
    /usr/share/wsjtx/sounds/Message.wav \
    /usr/share/wsjtx/sounds/Testing123.wav; do
    if ! linux_manifest_has_path "$manifest" "$path"; then
      linux_validation_error "$label is missing expected package path: $path"
      return 1
    fi
  done

  case $'\n'"$manifest"$'\n' in
    *"/usr/bin/sounds"$'\n'* | *"/usr/bin/sounds/"*)
      linux_validation_error "$label contains unexpected package path: /usr/bin/sounds"
      return 1
      ;;
  esac
}

validate_deb_package() {
  local package="$1"
  local manifest dependencies

  manifest=$(dpkg-deb --fsys-tarfile "$package" | tar -tf -) || return
  validate_linux_package_manifest "$manifest" "DEB $package" || return
  dependencies=$(dpkg-deb -f "$package" Depends Pre-Depends Recommends) || return
  validate_linux_boost_dependencies "DEB $package" "$dependencies" || return
  (
    local payload
    payload=$(mktemp -d) || exit
    trap 'rm -rf "$payload"' EXIT
    dpkg-deb -x "$package" "$payload" || exit
    validate_linux_static_boost_tree "$payload"
  )
}

validate_rpm_package() {
  local package="$1"
  local manifest dependencies

  manifest=$(rpm -qpl "$package") || return
  printf '%s\n' "$manifest" | sed -n '1,40p'
  validate_linux_package_manifest "$manifest" "RPM $package" || return
  dependencies=$(rpm -qpR "$package") || return
  validate_linux_boost_dependencies "RPM $package" "$dependencies" || return
  (
    local payload archive
    payload=$(mktemp -d) || exit
    trap 'rm -rf "$payload"' EXIT
    archive="$payload/payload.cpio"
    rpm2cpio "$package" > "$archive" || exit
    mkdir "$payload/root" || exit
    (cd "$payload/root" && cpio -idm --quiet < "$archive") || exit
    validate_linux_static_boost_tree "$payload/root"
  )
}

linux_require_executable() {
  local path="$1"
  if [ ! -x "$path" ]; then
    linux_validation_error "Required executable is missing: $path"
    return 1
  fi
}

linux_require_file() {
  local path="$1"
  if [ ! -f "$path" ]; then
    linux_validation_error "Required file is missing: $path"
    return 1
  fi
}

validate_linux_application_layout() {
  local root="$1"
  local kind="$2"
  local path

  for path in wsjtx jt9 qmap map65 ft8code; do
    linux_require_executable "$root/usr/bin/$path" || return
  done
  if [ "$kind" = appimage ]; then
    linux_require_executable "$root/usr/bin/wsprd" || return
  elif [ "$kind" != appdir ]; then
    linux_validation_error "Unknown application tree kind: $kind"
    return 2
  fi

  for path in \
    usr/share/wsjtx/ALLCALL7.TXT \
    usr/share/wsjtx/CALL3.TXT \
    usr/share/wsjtx/sounds/Message.wav \
    usr/share/wsjtx/sounds/Testing123.wav; do
    linux_require_file "$root/$path" || return
  done
  if [ -e "$root/usr/bin/sounds" ] || [ -L "$root/usr/bin/sounds" ]; then
    linux_validation_error "Unexpected executable-adjacent sounds path is present: $root/usr/bin/sounds"
    return 1
  fi

  if [ "$kind" = appdir ]; then
    linux_require_file "$root/usr/share/applications/wsjtx.desktop" || return
    linux_require_file "$root/usr/share/pixmaps/wsjtx_icon.png" || return
    return 0
  fi

  for path in \
    usr/bin/qt.conf \
    usr/plugins/platforms/libqxcb.so \
    usr/plugins/sqldrivers/libqsqlite.so; do
    linux_require_file "$root/$path" || return
  done
  if ! grep -Eq '^[[:space:]]*Plugins[[:space:]]*=[[:space:]]*plugins[[:space:]]*$' \
    "$root/usr/bin/qt.conf"; then
    linux_validation_error "AppImage qt.conf must set Plugins = plugins"
    echo "AppImage qt.conf contents:"
    cat "$root/usr/bin/qt.conf"
    return 1
  fi

  for path in libQt5Core.so libQt5Widgets.so libQt5Multimedia.so; do
    if [ -z "$(find "$root/usr/lib" -maxdepth 1 -name "$path*" -print -quit 2>/dev/null)" ]; then
      linux_validation_error "Required AppImage Qt library is missing: $path"
      return 1
    fi
  done

  if [ -f "$root/usr/lib/libgcrypt.so.20" ]; then
    linux_require_file "$root/usr/lib/libgpg-error.so.0" || return
    linux_require_file "$root/usr/share/doc/libgpg-error0/copyright" || return
  fi

  if [ -z "$(find "$root" -path '*/plugins/audio/*.so' -print -quit 2>/dev/null)" ]; then
    linux_validation_error "No Qt audio backend plugin is bundled in the AppImage"
    echo "AppImage plugin tree:"
    find "$root" -path '*/plugins/*' -name '*.so' -print 2>/dev/null | sed -n '1,20p'
    return 1
  fi
}

validate_linux_application_tree() {
  local root="$1"
  local kind="$2"
  local arch="$3"
  local executable

  validate_linux_application_layout "$root" "$kind" || return
  validate_linux_static_boost_tree "$root" || return
  for executable in wsjtx jt9 qmap map65 ft8code; do
    validate_elf_executable "$arch" "$root/usr/bin/$executable" || return
  done
  if [ "$kind" = appimage ]; then
    validate_elf_executable "$arch" "$root/usr/bin/wsprd" || return
    echo "Qt audio plugins bundled:"
    find "$root" -path '*/plugins/audio/*.so' -print 2>/dev/null
  fi
}

run_packaged_appimage_startup_smoke() {
  local appimage="$1"
  local log="$2"
  local status
  local runner=()
  if [ -n "${LINUX_APPIMAGE_RUNNER:-}" ]; then
    runner=("$LINUX_APPIMAGE_RUNNER")
  fi

  mkdir -p "$(dirname "$log")"
  if env \
    -u LD_LIBRARY_PATH \
    -u LD_PRELOAD \
    -u LD_AUDIT \
    -u QT_PLUGIN_PATH \
    -u QT_QPA_PLATFORM_PLUGIN_PATH \
    -u QML_IMPORT_PATH \
    -u QML2_IMPORT_PATH \
    APPIMAGE_EXTRACT_AND_RUN=1 \
    QT_QPA_PLATFORM=xcb \
    timeout --kill-after=5s 45s \
      xvfb-run -a -s "-screen 0 1280x1024x24" \
      "${runner[@]}" \
      "$appimage" \
      --startup-smoke-test \
      --rig-name CI-APPIMAGE-STARTUP \
      >"$log" 2>&1; then
    status=0
  else
    status=$?
  fi

  cat "$log"
  if [ "$status" -eq 124 ]; then
    linux_validation_error "AppImage startup smoke test timed out after 45 seconds"
    return 1
  fi
  if [ "$status" -ne 0 ]; then
    linux_validation_error "AppImage startup smoke test exited with status $status"
    return 1
  fi
  if ! grep -Fq "WSJT-X startup smoke test passed" "$log"; then
    linux_validation_error "AppImage exited successfully without the startup-smoke success marker"
    return 1
  fi
}
