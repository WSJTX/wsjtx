#!/usr/bin/env bash
set -euo pipefail

QT_VERSION=5.15.18
QT_SOURCE_SHA256=cea1fbabf02455f3f0e8eaa839f5d6f45cdb56b62c8a83af5c1d00ac05f912ea
QT_CACHE_EPOCH=1
QT_SOURCE_URL="https://download.qt.io/archive/qt/5.15/${QT_VERSION}/single/qt-everywhere-opensource-src-${QT_VERSION}.tar.xz"

macos_qt_script_dir() {
  cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1
  pwd
}

macos_qt_sha256_file() {
  if command -v shasum >/dev/null 2>&1; then
    shasum -a 256 "$1" | awk '{print $1}'
  elif command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$1" | awk '{print $1}'
  else
    echo "No SHA-256 tool found; expected shasum or sha256sum" >&2
    return 1
  fi
}

macos_qt_sha256_stream() {
  if command -v shasum >/dev/null 2>&1; then
    shasum -a 256 | awk '{print $1}'
  elif command -v sha256sum >/dev/null 2>&1; then
    sha256sum | awk '{print $1}'
  else
    echo "No SHA-256 tool found; expected shasum or sha256sum" >&2
    return 1
  fi
}

macos_qt_script_hash() {
  local script_dir
  script_dir="$(macos_qt_script_dir)"

  {
    for script in build-qt-macos.sh macos-qt-cache-key.sh; do
      local path="${script_dir}/${script}"
      if [ ! -f "$path" ]; then
        echo "Missing Qt cache input script: $path" >&2
        return 1
      fi
      printf '%s  %s\n' "$(macos_qt_sha256_file "$path")" "$script"
    done
  } | macos_qt_sha256_stream
}

macos_qt_cache_key() {
  local arch="$1"
  local runner="$2"
  local deployment_target="$3"
  local script_hash
  script_hash="$(macos_qt_script_hash)"

  printf 'qt-macos-%s-%s-%s-min%s-epoch%s-%s\n' \
    "$arch" \
    "$runner" \
    "$QT_VERSION" \
    "$deployment_target" \
    "$QT_CACHE_EPOCH" \
    "$script_hash"
}

usage() {
  cat <<'USAGE'
Usage: macos-qt-cache-key.sh --arch ARCH --runner RUNNER --deployment-target VERSION [--github-output]

Prints the exact GitHub Actions cache key for the pinned macOS Qt build.
USAGE
}

main() {
  local arch=""
  local runner=""
  local deployment_target=""
  local github_output=0

  while [ "$#" -gt 0 ]; do
    case "$1" in
      --arch)
        arch="$2"
        shift 2
        ;;
      --runner)
        runner="$2"
        shift 2
        ;;
      --deployment-target)
        deployment_target="$2"
        shift 2
        ;;
      --github-output)
        github_output=1
        shift
        ;;
      --help|-h)
        usage
        exit 0
        ;;
      *)
        echo "Unknown argument: $1" >&2
        usage >&2
        exit 2
        ;;
    esac
  done

  if [ -z "$arch" ] || [ -z "$runner" ] || [ -z "$deployment_target" ]; then
    usage >&2
    exit 2
  fi

  case "${arch}:${runner}:${deployment_target}" in
    arm64:macos-15:11.0|x86_64:macos-15-intel:10.13)
      ;;
    *)
      echo "Unsupported macOS Qt build tuple: arch=${arch}, runner=${runner}, deployment_target=${deployment_target}" >&2
      exit 2
      ;;
  esac

  local key
  key="$(macos_qt_cache_key "$arch" "$runner" "$deployment_target")"
  printf '%s\n' "$key"

  if [ "$github_output" -eq 1 ]; then
    if [ -z "${GITHUB_OUTPUT:-}" ]; then
      echo "--github-output requires GITHUB_OUTPUT to be set" >&2
      exit 2
    fi
    {
      printf 'key=%s\n' "$key"
      printf 'version=%s\n' "$QT_VERSION"
      printf 'source_url=%s\n' "$QT_SOURCE_URL"
      printf 'source_sha256=%s\n' "$QT_SOURCE_SHA256"
      printf 'cache_epoch=%s\n' "$QT_CACHE_EPOCH"
    } >> "$GITHUB_OUTPUT"
  fi
}

if [[ "${BASH_SOURCE[0]}" == "$0" ]]; then
  main "$@"
fi
