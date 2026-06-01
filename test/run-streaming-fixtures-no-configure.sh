#!/usr/bin/env bash
# Streaming-fixture regression harness — no-configure-frame variant.
#
# Exercises the CLI-arg threading path through init_default_params.
# Sends WSJL header + audio frames + halt to jt9 --stream
# WITHOUT a configure frame; jt9 reads its decode parameters from the
# CLI flags supplied to the binary directly.
#
# Earlier, streaming_io.f90's bulk init silently overrode CLI-supplied
# values like -F (ntol), -f (rxfreq), -L (flow), -H (fhigh), -d (ndepth)
# with FT8-baseline hardcoded defaults. A jt9 invocation without a
# configure frame produced 0 decodes for any non-default CLI args — the
# CLI args were lost. This harness locks in the fix.
#
# Usage:
#   test/run-streaming-fixtures-no-configure.sh [build-dir]

set -euo pipefail

BUILD_DIR="${1:-build}"
REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
JT9="$REPO_ROOT/$BUILD_DIR/jt9"
HARNESS="$REPO_ROOT/test/wav_to_stream_no_configure.py"

if [[ ! -x "$JT9" ]]; then
  echo "ERROR: jt9 binary not found or not executable: $JT9" >&2
  exit 2
fi
if [[ ! -f "$HARNESS" ]]; then
  echo "ERROR: harness not found: $HARNESS" >&2
  exit 2
fi

echo "Streaming-fixture regression (no-configure-frame variant)"
echo "  jt9:       $JT9"
echo "  harness:   $HARNESS"

fail=0

# Format: <wav-path> <mode-flag> <minimum-decode-count>
# CLI flags supplied: -F 1000 (ntol), -f 1500 (rxfreq), -L 200 (flow),
# -H 3000 (fhigh), -d 1 (ndepth) — values that would otherwise come from
# a configure frame. Targets the same FT8/JT9 fixtures used by the
# configure-frame harness so the two suites' decode-count expectations
# can be compared directly.
fixtures=(
  "samples/FT8/210703_133430.wav -8 14"
  "samples/JT9/130418_1742.wav -9 6"
)

for entry in "${fixtures[@]}"; do
  read -r wav mode_flag min_count <<< "$entry"
  wav_path="$REPO_ROOT/$wav"
  if [[ ! -f "$wav_path" ]]; then
    echo "  SKIP: $wav (sample not present)"
    continue
  fi

  count=$(python3 "$HARNESS" "$JT9" "$wav_path" \
    --mode-flag "$mode_flag" \
    --ntol 1000 --nrxfreq 1500 --flow 200 --fhigh 3000 --ndepth 1 \
    --quiet 2>/dev/null \
    | grep -c '"t":"decode"' || true)
  if [[ "$count" -ge "$min_count" ]]; then
    echo "  PASS: $wav ($mode_flag, $count decodes >= $min_count)"
  else
    echo "  FAIL: $wav ($mode_flag, $count decodes < $min_count expected)"
    fail=1
  fi
done

if [[ "$fail" -eq 0 ]]; then
  echo "All no-configure streaming fixtures passed."
else
  echo "One or more no-configure streaming fixtures regressed."
  exit 1
fi
