#!/usr/bin/env bash
# Phase 8 parse-unit regression (streaming-schema unit parse->pack assert).
# Compiles lib/streaming_control.f90 and the driver
# test/phase8_parse_test.f90 into an ISOLATED module dir and runs it. Guards the
# PARSE side of the 5 Phase 8 string & scaled / derived keys
# (utc/date/n_trials/candthin_threshold/dt_center_seconds): the ISO derivations
# (utc->nutc HHMMSS, date->yymmdd), the n_trials->nranera encoding table (incl. the
# n_trials=1 -> nranera=0 spec edge), the two PARSE-TIME VALUE errors (n_trials not
# in {10^N,3*10^N}; date year>=2100), wrong-TYPE detection, malformed-ISO silent
# skip, nutc+utc both captured, and the value-vs-key aliasing guard.
#
# WHY a Fortran unit driver (not only a fixture through jt9): four of the five
# Phase 8 keys are INERT on the FT8/JT9 fixtures (date superfox-only; n_trials
# JT65-deep-search-only; candthin_threshold/dt_center_seconds multithread-FT8-
# variant-only), so no decode-output gate can assert those
# derivations landed. (utc IS decode-observable — Gates 19/20.) This driver is the
# parse->pack assert for the PARSE side; the configure_fields ->
# params APPLY routing (incl. the utc-wins precedence + the /100 scaling) is
# asserted by test/run-apply-routing-unit.sh.
#
# Usage:
#   test/run-phase8-parse-unit.sh        (no build dir; compiles from source)
#   FC=gfortran-13 test/run-phase8-parse-unit.sh   (override the compiler)

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
FC="${FC:-gfortran}"
SRC="$REPO_ROOT/lib/streaming_control.f90"
DRV="$REPO_ROOT/test/phase8_parse_test.f90"

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

echo "Phase 8 parse-unit regression"
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
"$FC" "$DRV" "$WORK/streaming_control.o" -I"$WORK" -o "$WORK/p8drv"
"$WORK/p8drv"
