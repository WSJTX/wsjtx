#!/usr/bin/env bash
# Phase 9 parse-unit regression (streaming-schema unit parse->pack assert).
# Compiles lib/streaming_control.f90 and the driver
# test/phase9_parse_test.f90 into an ISOLATED module dir and runs it. Guards the
# PARSE + PACK side of the 9 bit-packed keys (depth_level/q65_maxiters_level/
# use_averaging/deep_ap_search/q65_auto_clear_average -> ndepth;
# contest_type/single_decode/vhf_features/noise_blanker_level -> nexp_decode): the
# per-bit packing, the contest_type -> ncontest table (incl. the SpecOp 5/8/9 -> 1
# collapse), the two atomic-decline errors (legacy depth co-present with an unpacked
# ndepth key; depth_level vs q65_maxiters_level inconsistency), the bit-width masking
# of depth_level/q65_maxiters, the noise_blanker -3 offset + [0,255] clamp,
# wrong-type detection, the unknown-contest silent skip, and value-vs-key aliasing.
#
# WHY a Fortran unit driver (not only fixtures through jt9): only depth_level is
# decode-observable on FT8/JT9 (Gate 24); the other 8 keys are INERT (Q65/JT65/
# FST4/JT4 or contest-branch only), and even depth_level's bit
# PACKING is invisible to a decode-count gate (ndepth=3 and ndepth=3|16 both decode
# 21 on FT8). This driver is the parse->pack assert; the
# configure_fields -> params APPLY routing (the packed ints -> ndepth/nexp_decode)
# is asserted by test/run-apply-routing-unit.sh.
#
# Usage:
#   test/run-phase9-parse-unit.sh        (no build dir; compiles from source)
#   FC=gfortran-13 test/run-phase9-parse-unit.sh   (override the compiler)

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
FC="${FC:-gfortran}"
SRC="$REPO_ROOT/lib/streaming_control.f90"
DRV="$REPO_ROOT/test/phase9_parse_test.f90"

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

echo "Phase 9 parse-unit regression"
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
"$FC" "$DRV" "$WORK/streaming_control.o" -I"$WORK" -o "$WORK/p9drv"
"$WORK/p9drv"
