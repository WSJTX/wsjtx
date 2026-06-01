#!/usr/bin/env bash
# Phase 4 parse-unit regression (streaming-schema unit parse->pack assert).
# Compiles lib/streaming_control.f90 and the driver
# test/phase4_parse_test.f90 into an ISOLATED module dir and runs it. Guards the
# PARSE side of the 9 Phase 4 keys: each key -> correct configure_fields member +
# value, bool true/false, his_call vs his_call_standard substring non-collision,
# wrong-type detection (bool/string/int), and the value-vs-key aliasing guard.
#
# WHY a Fortran unit driver (not a fixture through jt9): the 9 Phase 4 fields are
# observably INERT on the FT8/JT9 fixtures (tx/Q65/JT65-split only — an
# all-9-keys configure yields a byte-identical 21-message FT8 decode set), so no
# decode-output gate can assert a Phase 4 value landed. This unit driver is the
# parse->pack assert for inert fields.
#
# SCOPE: this guards JSON -> configure_fields PARSE only (the module-public entry
# point). The configure_fields -> params APPLY routing now lives in the module-
# public streaming_apply::apply_configure_fields and is asserted by
# test/run-apply-routing-unit.sh — so the 9 inert Phase 4 fields' apply routing
# IS now unit-guarded, not just code-review + no-regression.
#
# Usage:
#   test/run-phase4-parse-unit.sh        (no build dir; compiles from source)
#   FC=gfortran-13 test/run-phase4-parse-unit.sh   (override the compiler)

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
FC="${FC:-gfortran}"
SRC="$REPO_ROOT/lib/streaming_control.f90"
DRV="$REPO_ROOT/test/phase4_parse_test.f90"

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

echo "Phase 4 parse-unit regression"
echo "  FC:     $FC"
echo "  source: $SRC"
echo "  driver: $DRV"
echo ""

# Compile FROM the work dir as cwd: gfortran searches the current directory for
# .mod files first, so a stray repo-root streaming_control.mod would otherwise
# shadow the fresh compile (the -J/-I flags alone do NOT remove cwd from the
# search order; the cd does).
cd "$WORK"
"$FC" -c "$SRC" -J"$WORK" -o "$WORK/streaming_control.o"
"$FC" "$DRV" "$WORK/streaming_control.o" -I"$WORK" -o "$WORK/p4drv"
"$WORK/p4drv"
