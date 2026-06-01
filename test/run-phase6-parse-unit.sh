#!/usr/bin/env bash
# Phase 6 parse-unit regression (streaming-schema unit parse->pack assert).
# Compiles lib/streaming_control.f90 and the driver
# test/phase6_parse_test.f90 into an ISOLATED module dir and runs it. Guards the
# PARSE side of the 24 Phase 6 FT8/FT4-specifics keys (19 bools + 5 ints): each
# key -> correct configure_fields member + value, bool true/false, int parse,
# wrong-type detection (bool-as-number, int-as-string), the NEW "mode" substring
# non-collisions (hint_mode / hound_mode / superfox_mode all contain "mode"), and
# the value-vs-key aliasing guard.
#
# WHY a Fortran unit driver (not only a fixture through jt9): all 24 Phase 6 keys
# are INERT on the FT8/JT9 fixtures (multithreaded_ft8 is
# destructive and apply-guarded to ignore non-false; the other 23 are read only on
# the multithread-FT8 / JT65 / superfox-Q65 / WAV-disk paths or are AP keys for
# which the fixture has no AP-only decodes), so no decode-output gate can assert
# those values landed. This unit driver is the parse->pack assert
# for the PARSE side; the configure_fields -> params APPLY routing is asserted by
# test/run-apply-routing-unit.sh (the 24 Phase 6 slot asserts + the
# multithreaded_ft8 ignore-non-false guard).
#
# Usage:
#   test/run-phase6-parse-unit.sh        (no build dir; compiles from source)
#   FC=gfortran-13 test/run-phase6-parse-unit.sh   (override the compiler)

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
FC="${FC:-gfortran}"
SRC="$REPO_ROOT/lib/streaming_control.f90"
DRV="$REPO_ROOT/test/phase6_parse_test.f90"

if ! command -v "$FC" >/dev/null 2>&1; then
  echo "ERROR: Fortran compiler '$FC' not found (set \$FC to override)" >&2
  exit 2
fi
for f in "$SRC" "$DRV"; do
  if [[ ! -f "$f" ]]; then
    echo "ERROR: missing source: $f" >&2
    exit 2
  fi
done

# Compile into a throwaway dir so a stray repo-root *.mod cannot shadow the
# fresh compile, and so we leave no artifacts behind.
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

echo "Phase 6 parse-unit regression"
echo "  FC:     $FC"
echo "  source: $SRC"
echo "  driver: $DRV"
echo ""

# Compile FROM the work dir as cwd: gfortran searches the current directory for
# .mod files first, so a stray repo-root streaming_control.mod would otherwise
# shadow the fresh compile.
# -J/-I alone do NOT remove cwd from the search order; the cd does.
cd "$WORK"
"$FC" -c "$SRC" -J"$WORK" -o "$WORK/streaming_control.o"
"$FC" "$DRV" "$WORK/streaming_control.o" -I"$WORK" -o "$WORK/p6drv"
"$WORK/p6drv"
