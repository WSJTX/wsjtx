#!/usr/bin/env bash
# Configure-protocol regression harness — nutc thread-through, nfa/nfb
# mode-change restore, and ntol widening transition.
#
# Three gates:
#   1. nutc thread-through: verify NDJSON time field reflects sent nutc.
#   2. Mode-change ntol restore: prepend Q65 configure (auto-sets ntol=10),
#      then FT8 configure with no explicit ntol. Buggy behavior: FT8 decodes
#      with ntol=10 → ±10 Hz window → near-zero decodes. Fixed behavior: ntol
#      restores to 1000 → full FT8 decode count.
#   3. Mode-change nfa/nfb restore: prepend Q65 configure with explicit
#      narrow nfa/nfb (1400/1600) AND ntol=10, then FT8 configure with no
#      explicit nfa/nfb/ntol. Buggy behavior: FT8 inherits the narrow window
#      → near-zero decodes. Fixed behavior: both ntol AND nfa/nfb restore.
#
# Usage:
#   test/run-pr3-fixtures.sh [build-dir]

set -euo pipefail

BUILD_DIR="${1:-build}"
REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
JT9="$REPO_ROOT/$BUILD_DIR/jt9"
HARNESS="$REPO_ROOT/test/wav_to_stream_harness.py"

if [[ ! -x "$JT9" ]]; then
  echo "ERROR: jt9 binary not found or not executable: $JT9" >&2
  exit 2
fi
if [[ ! -f "$HARNESS" ]]; then
  echo "ERROR: harness not found: $HARNESS" >&2
  exit 2
fi

echo "Configure-protocol regression (nutc + nfa/nfb + ntol)"
echo "  jt9:       $JT9"
echo "  harness:   $HARNESS"

fail=0

# --- Gate 1: nutc thread-through ----------------------------------
echo ""
echo "Gate 1 — nutc thread-through"
nutc_count=$(python3 "$HARNESS" "$JT9" "$REPO_ROOT/samples/FT8/210703_133430.wav" \
  --mode FT8 --nutc 133430 --quiet 2>/dev/null \
  | grep -c '"time":"133430"' || true)
if [[ "$nutc_count" -ge 14 ]]; then
  echo "  PASS: nutc=133430 reflected in $nutc_count decode records"
else
  echo "  FAIL: nutc=133430 only reflected in $nutc_count decode records (expected >= 14)"
  fail=1
fi

# --- Gate 2: mode-change ntol restore -----------------------------
echo ""
echo "Gate 2 — mode-change ntol restore"
ntol_count=$(python3 "$HARNESS" "$JT9" "$REPO_ROOT/samples/FT8/210703_133430.wav" \
  --mode FT8 --prepend-mode Q65 --quiet 2>/dev/null \
  | grep -c '"t":"decode"' || true)
if [[ "$ntol_count" -ge 14 ]]; then
  echo "  PASS: Q65→FT8 (no explicit ntol): $ntol_count decodes >= 14"
  echo "        ntol restored from Q65's hard-10 to FT8's default 1000"
else
  echo "  FAIL: Q65→FT8 (no explicit ntol): $ntol_count decodes < 14 expected"
  echo "        ntol likely stayed at Q65's 10, narrowing FT8 window"
  fail=1
fi

# --- Gate 3: mode-change nfa/nfb restore --------------------------
echo ""
echo "Gate 3 — mode-change nfa/nfb restore"
nfa_count=$(python3 "$HARNESS" "$JT9" "$REPO_ROOT/samples/FT8/210703_133430.wav" \
  --mode FT8 --prepend-mode Q65 --prepend-with-explicit-narrowing \
  --quiet 2>/dev/null \
  | grep -c '"t":"decode"' || true)
if [[ "$nfa_count" -ge 14 ]]; then
  echo "  PASS: Q65 (narrow nfa=1400 nfb=1600 ntol=10) → FT8 (no override):"
  echo "        $nfa_count decodes >= 14"
  echo "        Both nfa/nfb (session baseline) AND ntol (per-mode default) restored"
else
  echo "  FAIL: Q65 (narrow) → FT8: $nfa_count decodes < 14 expected"
  echo "        Either nfa/nfb stayed narrow or ntol stayed at Q65's 10"
  fail=1
fi

echo ""
if [[ "$fail" -eq 0 ]]; then
  echo "All configure-protocol fixtures passed."
else
  echo "One or more configure-protocol fixtures regressed."
  exit 1
fi
