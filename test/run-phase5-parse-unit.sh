#!/usr/bin/env bash
# Phase 5 parse-unit regression (streaming-schema unit parse->pack assert).
# Compiles lib/streaming_control.f90 and the driver
# test/phase5_parse_test.f90 into an ISOLATED module dir and runs it. Guards the
# PARSE side of the 12 Phase 5 decoder-tuning keys: each key -> correct
# configure_fields member + value, real(8) keys, the tx_mode enum (JT9/JT65 -> 9/
# 65 and raw int), bool true/false, wrong-type detection (real/int/bool), the
# "mode" vs "tx_mode" + "min_width" vs "min_sync" substring non-collision, and
# the value-vs-key aliasing guard.
#
# WHY a Fortran unit driver (not only a fixture through jt9): 10 of the 12 Phase 5
# fields are observably INERT on the FT8/JT9 fixtures (kin/nzhsym
# are recomputed per period; dttol/minw/minsync/n2pass/nrobust/nclearave are
# JT4/JT65/Q65-only; emedelay is forced to 0 on a mode-setting frame; ntxmode is
# read only in mode 65+9), so no decode-output gate can assert those values
# landed. This unit driver is the parse->pack assert for them.
# The two observable keys (nagain_flag -> FT8, npts_c0_array -> JT9) ALSO get a
# decode-effect gate in run-schema-v1-fixtures.sh (Gates 11/12).
#
# SCOPE: this guards JSON -> configure_fields PARSE only (the module-public entry
# point). The configure_fields -> params APPLY routing now lives in the module-
# public streaming_apply::apply_configure_fields and is asserted by
# test/run-apply-routing-unit.sh — so the 10 inert keys' apply routing IS now
# unit-guarded, not just code-review + no-regression.
#
# Usage:
#   test/run-phase5-parse-unit.sh        (no build dir; compiles from source)
#   FC=gfortran-13 test/run-phase5-parse-unit.sh   (override the compiler)

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
FC="${FC:-gfortran}"
SRC="$REPO_ROOT/lib/streaming_control.f90"
DRV="$REPO_ROOT/test/phase5_parse_test.f90"

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

echo "Phase 5 parse-unit regression"
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
"$FC" "$DRV" "$WORK/streaming_control.o" -I"$WORK" -o "$WORK/p5drv"
"$WORK/p5drv"
