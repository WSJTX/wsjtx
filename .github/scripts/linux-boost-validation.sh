#!/usr/bin/env bash
# Validate the pinned Boost dependency prefix and generated build inputs.

linux_boost_validation_error() {
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
    *) linux_boost_validation_error "Unsupported Boost archive architecture: $arch"; return 2 ;;
  esac
  for component in log_setup log filesystem thread atomic chrono container; do
    archive="$root/lib/libboost_$component.a"
    if [ ! -f "$archive" ]; then
      linux_boost_validation_error "Missing genuine Boost static archive: $archive"
      return 1
    fi
  done
  for archive in "$root"/lib/libboost_*.a; do
    if [ ! -f "$archive" ] || [ "$(head -c 7 "$archive")" != '!<arch>' ]; then
      linux_boost_validation_error "Missing genuine Boost static archive: $archive"
      return 1
    fi
    members=$("$ar_command" t "$archive") || return
    if [ -z "$members" ]; then
      linux_boost_validation_error "Required Boost archive has no object members: $archive"
      return 1
    fi
    headers=$("$readelf_command" -hW "$archive") || return
    machine=$(printf '%s\n' "$headers" | awk '/Machine:/ { sub(/^.*Machine:[[:space:]]*/, ""); print }' | sort -u)
    if [ "$machine" != "$expected" ]; then
      linux_boost_validation_error "$archive has unexpected object architectures: $machine (expected $expected)"
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

