#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'USAGE'
Usage: verify-macos-artifacts.sh --target VERSION --arch ARCH [--allow-prefix PATH ...] PATH...

Verifies Mach-O files under PATH for:
  * expected architecture
  * deployment target not newer than --target
  * no package-manager runtime library references
  * no Qt load dependencies in jt9
  * no Boost runtime dependencies or bundled Boost library files

Package-manager paths are always rejected:
  /opt/homebrew, /usr/local/Cellar, /usr/local/opt, /opt/local
USAGE
}

target=""
arch=""
allow_prefixes=()
paths=()

while [ "$#" -gt 0 ]; do
  case "$1" in
    --target)
      target="$2"
      shift 2
      ;;
    --arch)
      arch="$2"
      shift 2
      ;;
    --allow-prefix)
      allow_prefixes+=("$2")
      shift 2
      ;;
    --help|-h)
      usage
      exit 0
      ;;
    --)
      shift
      while [ "$#" -gt 0 ]; do
        paths+=("$1")
        shift
      done
      ;;
    -*)
      echo "Unknown option: $1" >&2
      usage >&2
      exit 2
      ;;
    *)
      paths+=("$1")
      shift
      ;;
  esac
done

if [ -z "$target" ] || [ -z "$arch" ] || [ "${#paths[@]}" -eq 0 ]; then
  usage >&2
  exit 2
fi

# Validate that all target paths exist before checking
for path in "${paths[@]}"; do
  if [ ! -e "$path" ]; then
    echo "::error::Path does not exist: $path" >&2
    exit 2
  fi
done


version_gt() {
  awk -v a="$1" -v b="$2" '
    BEGIN {
      split(a, av, ".")
      split(b, bv, ".")
      for (i = 1; i <= 3; i++) {
        ai = av[i] + 0
        bi = bv[i] + 0
        if (ai > bi) exit 0
        if (ai < bi) exit 1
      }
      exit 1
    }'
}

is_macho() {
  local file="$1"
  local file_type
  file_type=$(file "$file" 2>/dev/null || true)
  [[ "$file_type" == *"Mach-O"* || "$file_type" == *"universal binary"* ]]
}

is_allowed_reference() {
  local ref="$1"
  case "$ref" in
    @rpath/*|@loader_path/*|@executable_path/*|/usr/lib/*|/System/Library/*)
      return 0
      ;;
  esac

  local prefix
  if [ "${#allow_prefixes[@]}" -gt 0 ]; then
    for prefix in "${allow_prefixes[@]}"; do
      case "$ref" in
        "$prefix"/*) return 0 ;;
      esac
    done
  fi

  return 1
}

check_file() {
  local file="$1"
  local fail=0
  local archs
  local minos
  local refs
  local install_name

  archs=$(lipo -archs "$file" 2>/dev/null || true)
  if [ -z "$archs" ]; then
    echo "::error file=${file}::Could not determine Mach-O architecture"
    return 1
  fi
  case " ${archs} " in
    *" ${arch} "*) ;;
    *)
    echo "::error file=${file}::Expected architecture ${arch}, found: ${archs}"
    return 1
      ;;
  esac

  minos=$(otool -arch "$arch" -l "$file" 2>/dev/null | awk '
    /LC_BUILD_VERSION/ { in_build = 1; next }
    in_build && /minos/ { print $2; exit }
    /LC_VERSION_MIN_MACOSX/ { in_old = 1; next }
    in_old && /version/ { print $2; exit }
  ')
  if [ -n "$minos" ] && version_gt "$minos" "$target"; then
    echo "::error file=${file}::Requires macOS ${minos}, newer than artifact target ${target}"
    fail=1
  fi

  install_name=$(otool -arch "$arch" -D "$file" 2>/dev/null | awk 'NR == 2 { print $1 }' || true)
  if ! refs=$(otool -arch "$arch" -L "$file" 2>/dev/null | awk 'NR > 1 { print $1 }'); then
    echo "::error file=${file}::Could not inspect Mach-O load dependencies"
    return 1
  fi
  while IFS= read -r ref; do
    [ -n "$ref" ] || continue
    case "${ref##*/}" in
      libboost*.dylib|libboost*.so*)
        echo "::error file=${file}::Boost must be linked statically: ${ref}"
        fail=1
        ;;
    esac
    if [ "${file##*/}" = jt9 ]; then
      case "${ref##*/}" in
        Qt*|libQt*)
          echo "::error file=${file}::jt9 must not depend on Qt: ${ref}"
          fail=1
          ;;
      esac
    fi
    if [ "$ref" = "$install_name" ]; then
      case "$ref" in
        */*) ;;
        *) continue ;;
      esac
    fi
    case "$ref" in
      /opt/homebrew/*|/usr/local/Cellar/*|/usr/local/opt/*|/opt/local/*)
        echo "::error file=${file}::Package-manager runtime reference is not allowed: ${ref}"
        fail=1
        continue
        ;;
    esac
    if ! is_allowed_reference "$ref"; then
      echo "::error file=${file}::Unexpected runtime reference: ${ref}"
      fail=1
    fi
  done <<< "$refs"

  return "$fail"
}

overall=0
while IFS= read -r -d '' file; do
  case "${file##*/}" in
    libboost*.dylib|libboost*.so*|libboost*.a)
      echo "::error file=${file}::Bundled Boost library is not allowed"
      overall=1
      ;;
  esac
  if is_macho "$file"; then
    check_file "$file" || overall=1
  fi
done < <(
  for path in "${paths[@]}"; do
    if [ -d "$path" ]; then
      find "$path" \( -type f -o -type l \) -print0
    elif [ -f "$path" ]; then
      printf '%s\0' "$path"
    fi
  done
)

if [ "$overall" -ne 0 ]; then
  exit "$overall"
fi

echo "All checked Mach-O files match arch ${arch}, target macOS ${target}, and approved runtime paths."
