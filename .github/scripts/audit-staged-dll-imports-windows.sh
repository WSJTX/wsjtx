#!/usr/bin/env bash
set -u

stage_bin="${1:?usage: audit-staged-dll-imports-windows.sh <stage-bin> <exe> [exe...]}"
shift

if [ ! -d "$stage_bin" ]; then
  echo "::error::staged bin directory not found: $stage_bin"
  exit 1
fi

if [ "$#" -eq 0 ]; then
  echo "::error::no executables supplied for DLL import audit"
  exit 1
fi

fail=0

staged_lower=$(
  for dll in "$stage_bin"/*.dll "$stage_bin"/*.DLL; do
    [ -e "$dll" ] || continue
    basename "$dll" | tr '[:upper:]' '[:lower:]'
  done | sort -u
)

echo "staged DLLs in $stage_bin:"
if [ -n "$staged_lower" ]; then
  echo "$staged_lower"
else
  echo "(none)"
fi

for exe in "$@"; do
  if [ ! -f "$exe" ]; then
    echo "::error::executable not found for DLL import audit: $exe"
    fail=1
    continue
  fi

  echo "::group::DLL imports for $exe"
  imports=$(
    objdump -p "$exe" |
      awk '/DLL Name:/ {print $3}' |
      sort -u
  )

  if [ -n "$imports" ]; then
    echo "$imports"
  else
    echo "(none)"
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
