#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'USAGE'
Usage: verify-macos-static-archives.sh --target VERSION --arch ARCH [--mode error|warn] PATH...

Verifies static archives before they are linked into deployable macOS binaries:
  * expected architecture
  * object-member deployment targets not newer than --target

--mode error (default) fails the build on any finding. --mode warn keeps
structural problems (missing archive, wrong architecture) fatal but
downgrades object-member deployment-target findings to warning annotations
and exits 0. Member deployment targets in third-party toolchain archives
are outside this workflow's control, so only that uncertain signal is made
non-blocking; a missing input or wrong-arch archive is broken plumbing and
always fails.
USAGE
}

target=""
arch=""
mode="error"
archives=()
max_member_errors=25

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
    --mode)
      mode="$2"
      shift 2
      ;;
    --help|-h)
      usage
      exit 0
      ;;
    --)
      shift
      while [ "$#" -gt 0 ]; do
        archives+=("$1")
        shift
      done
      ;;
    -*)
      echo "Unknown option: $1" >&2
      usage >&2
      exit 2
      ;;
    *)
      archives+=("$1")
      shift
      ;;
  esac
done

if [ -z "$target" ] || [ -z "$arch" ] || [ "${#archives[@]}" -eq 0 ]; then
  usage >&2
  exit 2
fi

case "$mode" in
  error|warn) ;;
  *)
    echo "Invalid --mode: $mode (expected error or warn)" >&2
    exit 2
    ;;
esac

# Annotation level for findings: errors fail the build, warnings do not.
if [ "$mode" = "warn" ]; then
  ann="warning"
else
  ann="error"
fi

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

check_archive() {
  local archive="$1"
  local structural_fail=0
  local target_fail=0
  local archs
  local line
  local member=""
  local in_build=0
  local in_old=0
  local minos
  local reported=0
  local suppressed=0

  if [ ! -f "$archive" ]; then
    echo "::error::Static archive does not exist: ${archive}"
    return 2
  fi

  archs=$(lipo -archs "$archive" 2>/dev/null || true)
  if [ -n "$archs" ] && ! printf '%s\n' "$archs" | tr ' ' '\n' | grep -qx "$arch"; then
    echo "::error file=${archive}::Expected architecture ${arch}, found: ${archs}"
    structural_fail=1
  fi

  while IFS= read -r line; do
    case "$line" in
      *".a("*"):")
        member="${line%:}"
        in_build=0
        in_old=0
        ;;
      *"LC_BUILD_VERSION"*)
        in_build=1
        in_old=0
        ;;
      *"LC_VERSION_MIN_MACOSX"*)
        in_build=0
        in_old=1
        ;;
      *" minos "*)
        if [ "$in_build" -eq 1 ]; then
          read -r _ minos _ <<< "$line"
          if version_gt "$minos" "$target"; then
            if [ "$reported" -lt "$max_member_errors" ]; then
              echo "::${ann} file=${archive}::${member} requires macOS ${minos}, newer than archive target ${target}"
            else
              suppressed=$((suppressed + 1))
            fi
            reported=$((reported + 1))
            target_fail=1
          fi
          in_build=0
        fi
        ;;
      *" version "*)
        if [ "$in_old" -eq 1 ]; then
          read -r _ minos _ <<< "$line"
          if version_gt "$minos" "$target"; then
            if [ "$reported" -lt "$max_member_errors" ]; then
              echo "::${ann} file=${archive}::${member} requires macOS ${minos}, newer than archive target ${target}"
            else
              suppressed=$((suppressed + 1))
            fi
            reported=$((reported + 1))
            target_fail=1
          fi
          in_old=0
        fi
        ;;
    esac
  done < <(otool -arch "$arch" -l "$archive" 2>/dev/null || true)

  if [ "$suppressed" -gt 0 ]; then
    echo "::${ann} file=${archive}::Suppressed ${suppressed} additional archive member deployment-target findings"
  fi

  # 2 = structural problem (always fatal), 1 = deployment-target finding
  # (respects --mode), 0 = clean.
  if [ "$structural_fail" -ne 0 ]; then
    return 2
  fi
  if [ "$target_fail" -ne 0 ]; then
    return 1
  fi
  return 0
}

overall_structural=0
overall_target=0
for archive in "${archives[@]}"; do
  rc=0
  check_archive "$archive" || rc=$?
  case "$rc" in
    2) overall_structural=1 ;;
    1) overall_target=1 ;;
  esac
done

if [ "$overall_structural" -ne 0 ]; then
  echo "::error::Static archive verification failed: missing archive or wrong architecture is always fatal."
  exit 1
fi

if [ "$overall_target" -ne 0 ]; then
  if [ "$mode" = "error" ]; then
    exit 1
  fi
  echo "::notice::Static archive verification reported deployment-target findings in warn mode; not failing the build."
  exit 0
fi

echo "All checked static archives match arch ${arch} and target macOS ${target}."
