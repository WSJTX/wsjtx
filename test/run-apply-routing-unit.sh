#!/usr/bin/env bash
# apply-routing unit regression. Compiles
# lib/streaming_control.f90 + lib/jt9_params_init.f90 + lib/streaming_apply.f90
# and the driver test/apply_routing_test.f90 into an ISOLATED module dir and runs
# it. Guards the APPLY side: each configure_fields member -> its CORRECT
# params_block slot, plus the order-sensitive logic (rxfreq/tx_audio_offset
# decouple, emedelay-before-per-mode-policy, nfa/nfb baseline restore, ntol
# per-mode cap/restore).
#
# WHY a Fortran unit driver: the cfg->params apply routing used to live in a
# nested INTERNAL subroutine (streaming_io.f90 apply_configure_), unreachable from
# a unit driver, AND most Phase 4/5 fields are inert on the FT8/JT9 fixtures, so a
# mis-route (e.g. his_call -> hisgrid) passed every fixture gate byte-identical.
# The mapping was extracted to the module-public
# streaming_apply::apply_configure_fields so this driver can assert it directly.
# This is the structural close of the apply-routing gap.
#
# Usage:
#   test/run-apply-routing-unit.sh        (no build dir; compiles from source)
#   FC=gfortran-13 test/run-apply-routing-unit.sh   (override the compiler)

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
FC="${FC:-gfortran}"
SRC_CTRL="$REPO_ROOT/lib/streaming_control.f90"
SRC_INIT="$REPO_ROOT/lib/jt9_params_init.f90"
SRC_APPLY="$REPO_ROOT/lib/streaming_apply.f90"
DRV="$REPO_ROOT/test/apply_routing_test.f90"

if ! command -v "$FC" >/dev/null 2>&1; then
  echo "ERROR: Fortran compiler '$FC' not found (set \$FC to override)" >&2
  exit 2
fi
for f in "$SRC_CTRL" "$SRC_INIT" "$SRC_APPLY" "$DRV" \
         "$REPO_ROOT/lib/jt9com.f90" "$REPO_ROOT/lib/constants.f90"; do
  if [[ ! -f "$f" ]]; then
    echo "ERROR: missing source: $f" >&2
    exit 2
  fi
done

# Compile into a throwaway dir so a stray repo-root *.mod cannot shadow the fresh
# compile, and so we leave no artifacts behind.
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

echo "apply-routing unit regression"
echo "  FC:     $FC"
echo "  driver: $DRV"
echo ""

# Copy the textual includes (jt9com.f90 -> constants.f90) into the work dir so the
# INCLUDE chain resolves with cwd=$WORK and lib/ never lands on the search path.
cp "$REPO_ROOT/lib/jt9com.f90" "$REPO_ROOT/lib/constants.f90" "$WORK/"

# Compile FROM the work dir as cwd: gfortran searches the current directory for
# .mod files (and INCLUDE files) first, so a stray repo-root *.mod / jt9com.f90
# would otherwise shadow the fresh compile. -J/-I alone
# do NOT remove cwd from the search order; the cd does.
cd "$WORK"
"$FC" -c "$SRC_CTRL"  -J"$WORK" -I"$WORK" -o "$WORK/streaming_control.o"
"$FC" -c "$SRC_INIT"  -J"$WORK" -I"$WORK" -o "$WORK/jt9_params_init.o"
"$FC" -c "$SRC_APPLY" -J"$WORK" -I"$WORK" -o "$WORK/streaming_apply.o"
"$FC" "$DRV" "$WORK/streaming_control.o" "$WORK/jt9_params_init.o"            \
      "$WORK/streaming_apply.o" -I"$WORK" -o "$WORK/apply_drv"
"$WORK/apply_drv"
