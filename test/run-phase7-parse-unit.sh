#!/usr/bin/env bash
# Phase 7 parse-unit regression (streaming-schema unit parse->pack assert).
# Compiles lib/streaming_control.f90 and the driver
# test/phase7_parse_test.f90 into an ISOLATED module dir and runs it. Guards the
# PARSE side of the 6 Phase 7 diagnostic / sequencing keys (4 ints + 2 bools):
# each key -> correct configure_fields member + value, bool true/false, int
# parse, wrong-type detection (int-as-string, bool-as-number), the "mode"
# substring non-collision (mode_changed contains "mode"), and the value-vs-key
# aliasing guard.
#
# WHY a Fortran unit driver (not only a fixture through jt9): all 6 Phase 7 keys
# are INERT on the FT8/JT9 fixtures (five are read only inside
# the multithreaded-FT8 block decoder.f90:192..1148 which streaming forces off;
# qso_progress_state reaches the single-pass FT8 decoder but only steers AP
# decoding passes and the fixture has no AP-only decodes), so no decode-output
# gate can assert those values landed. This unit driver is the
# parse->pack assert for the PARSE side; the configure_fields -> params APPLY
# routing is asserted by test/run-apply-routing-unit.sh (the 6 Phase 7 slot
# asserts).
#
# Usage:
#   test/run-phase7-parse-unit.sh        (no build dir; compiles from source)
#   FC=gfortran-13 test/run-phase7-parse-unit.sh   (override the compiler)

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
FC="${FC:-gfortran}"
SRC="$REPO_ROOT/lib/streaming_control.f90"
DRV="$REPO_ROOT/test/phase7_parse_test.f90"

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

echo "Phase 7 parse-unit regression"
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
"$FC" "$DRV" "$WORK/streaming_control.o" -I"$WORK" -o "$WORK/p7drv"
"$WORK/p7drv"
