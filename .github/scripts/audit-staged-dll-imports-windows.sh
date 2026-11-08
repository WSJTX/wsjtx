#!/usr/bin/env bash
set -u

stage_bin="${1:?usage: audit-staged-dll-imports-windows.sh <stage-directory> [image...]}"
shift

if [ ! -d "$stage_bin" ]; then
  echo "::error::staged bin directory not found: $stage_bin"
  exit 1
fi

fail=0
images=()
staged_dlls=()

collect_images() {
  local entry lower
  for entry in "$1"/*; do
    [ -e "$entry" ] || continue
    if [ -d "$entry" ]; then
      collect_images "$entry"
      continue
    fi
    lower=$(basename "$entry" | tr '[:upper:]' '[:lower:]')
    case "$lower" in
      *boost*.dll|*boost*.a|*boost*.lib)
        echo "::error::Boost library must not be bundled in static release: $entry"
        fail=1
        ;;
    esac
    case "$lower" in
      *.dll) staged_dlls+=("$lower"); images+=("$entry") ;;
      *.exe) images+=("$entry") ;;
    esac
  done
}

collect_images "$stage_bin"
for image in "$@"; do
  case "$image" in
    "$stage_bin"/*) [ -f "$image" ] && continue ;;
  esac
  images+=("$image")
done

if [ "${#images[@]}" -eq 0 ]; then
  echo "::error::no executable or DLL images found for import audit"
  exit 1
fi

staged_lower=$(
  printf '%s\n' "${staged_dlls[@]}" | sort -u
)

echo "staged DLLs in $stage_bin:"
if [ -n "$staged_lower" ]; then
  echo "$staged_lower"
else
  echo "(none)"
fi

for exe in "${images[@]}"; do
  if [ ! -f "$exe" ]; then
    echo "::error::executable not found for DLL import audit: $exe"
    fail=1
    continue
  fi

  echo "::group::DLL imports for $exe"
  if ! import_metadata=$(objdump -p "$exe"); then
    echo "::error::Could not inspect DLL imports for $exe"
    echo "::endgroup::"
    fail=1
    continue
  fi
  imports=$(
    printf '%s\n' "$import_metadata" |
      awk '/DLL Name:/ {print $3}' |
      sort -u
  )

  if [ -n "$imports" ]; then
    echo "$imports"
  else
    echo "(none)"
  fi

  boost_imports=$(printf '%s\n' "$imports" | awk 'tolower($0) ~ /boost.*\.dll$/')
  if [ -n "$boost_imports" ]; then
    echo "::error::$exe imports shared Boost: $boost_imports"
    fail=1
  fi

  case "$(basename "$exe" | tr '[:upper:]' '[:lower:]')" in
    jt9.exe|wsjt-codec.exe)
      qt_imports=$(printf '%s\n' "$imports" | awk 'tolower($0) ~ /^qt.*\.dll$/')
      if [ -n "$qt_imports" ]; then
        echo "::error::$exe must not depend on Qt: $qt_imports"
        fail=1
      fi
      ;;
  esac

  win7_blocked_imports=$(
    printf '%s\n' "$import_metadata" |
      awk '
        /DLL Name:/ { dll=tolower($3) }
        /api-ms-win-core-synch-l1-2-0\.dll/ { print "api-ms-win-core-synch-l1-2-0.dll" }
        /WaitOnAddress/ { print dll "::WaitOnAddress" }
        /WakeByAddressSingle/ { print dll "::WakeByAddressSingle" }
        /WakeByAddressAll/ { print dll "::WakeByAddressAll" }
      ' |
      sort -u
  )

  if [ -n "$win7_blocked_imports" ]; then
    echo "::error::$exe imports Windows 8+ synchronization APIs that block Windows 7 compatibility:"
    echo "$win7_blocked_imports"
    fail=1
  fi

  high_risk=$(
    echo "$imports" |
      awk '{ lower=tolower($0); if (lower ~ /^(libgomp|libgfortran|libgcc_s|libquadmath|libportaudio|libhamlib|libfftw|qt5.*\.dll)/) print lower }' |
      sort -u
  )

  for dll in $high_risk; do
    if ! printf '%s\n' "$staged_lower" | awk -v wanted="$dll" '$0 == wanted { found=1 } END { exit found ? 0 : 1 }'; then
      echo "::error::$exe imports $dll, but $dll is missing from $stage_bin"
      fail=1
    fi
  done
  echo "::endgroup::"
done

if [ "$fail" -ne 0 ]; then
  echo "::group::staged bin contents"
  ls -la "$stage_bin"
  echo "::endgroup::"
fi

exit "$fail"
