#!/usr/bin/env bash
# wsprd streaming-fixture regression harness.
#
# Pipes the same WSPR fixture used by file-mode through `wsprd -0`
# (streaming mode) via test/wav_to_wsprd_stream_harness.py and asserts
# that the streaming-mode decode count meets or exceeds the file-mode
# ground truth.
#
# Usage:
#   test/run-wsprd-streaming-fixtures.sh [build-dir]

set -euo pipefail

BUILD_DIR="${1:-build}"
REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WSPRD="$REPO_ROOT/$BUILD_DIR/wsprd"
HARNESS="$REPO_ROOT/test/wav_to_wsprd_stream_harness.py"

if [[ ! -x "$WSPRD" ]]; then
  echo "ERROR: wsprd binary not found or not executable: $WSPRD" >&2
  exit 2
fi
if [[ ! -f "$HARNESS" ]]; then
  echo "ERROR: harness not found: $HARNESS" >&2
  exit 2
fi

echo "wsprd streaming-fixture regression"
echo "  wsprd:     $WSPRD"
echo "  harness:   $HARNESS"

fail=0

# Format: <wav-path> <dialfreq-MHz> <wspr-type> <minimum-decode-count>
fixtures=(
  "samples/WSPR/150426_0918.wav 14.0956 2 9"
)

for entry in "${fixtures[@]}"; do
  read -r wav dialfreq wspr_type min_count <<< "$entry"
  wav_path="$REPO_ROOT/$wav"
  if [[ ! -f "$wav_path" ]]; then
    echo "  SKIP: $wav (sample not present)"
    continue
  fi

  count=$(python3 "$HARNESS" "$WSPRD" "$wav_path" \
    --dialfreq "$dialfreq" --wspr-type "$wspr_type" \
    --quiet 2>/dev/null \
    | grep -c '"t":"decode"' || true)
  if [[ "$count" -ge "$min_count" ]]; then
    echo "  PASS: $wav (WSPR-$wspr_type, $count decodes >= $min_count)"
  else
    echo "  FAIL: $wav (WSPR-$wspr_type, $count decodes < $min_count expected)"
    fail=1
  fi
done

if [[ "$fail" -eq 0 ]]; then
  echo "All wsprd streaming fixtures passed."
else
  echo "One or more wsprd streaming fixtures regressed."
  exit 1
fi
