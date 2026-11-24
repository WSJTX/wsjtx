#!/usr/bin/env bash
# Compile and run the Phase 4 configure parser checks in isolation.
# Apply routing is covered by run-apply-routing-unit.sh.
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
source "$REPO_ROOT/test/compile-streaming-control.sh"
"$FC" "$DRV" "$WORK/streaming_control.o" -I"$WORK" -o "$WORK/p4drv"
"$WORK/p4drv"
