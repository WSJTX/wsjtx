#!/usr/bin/env bash
# External-producer streaming CONTRACT fixture.
#
# Pins the JOINT wire contract: pipes the EXACT byte stream a minimal external
# producer produces (WSJL header + the minimal versionless
# 6-key configure frame {t,mode,depth,trperiod,mycall,mygrid} + int16@12k
# audio + halt) through `jt9 --stream` and asserts the streaming decoder accepts
# it (no decline/error), emits `ready`, and emits decodes carrying every field
# such a front-end reads (message/mode/snr/dt/freq/time).
#
# Why this exists: an external front-end's own unit tests may use a printf fake
# (never a real jt9), and run-streaming-fixtures.sh sends the
# RICHER harness frame (ntol/nfa/version/...), not the minimal frame an external
# producer actually emits. Neither side alone proves the real producer format is
# accepted.
#
# If this fixture fails, the external-producer streaming contract has drifted:
# inspect test/radcon_contract_harness.py (the mirror of the external producer)
# against the current producer, and the streaming consumer
# (lib/streaming_io.f90 / streaming_control.f90 / streaming_emit.f90).
#
# Usage:
#   test/run-radcon-contract-fixture.sh [build-dir]
# build-dir defaults to ./build and must contain a built jt9 binary.

set -euo pipefail

BUILD_DIR="${1:-build}"
REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
JT9="$REPO_ROOT/$BUILD_DIR/jt9"
HARNESS="$REPO_ROOT/test/radcon_contract_harness.py"

if [[ ! -x "$JT9" ]]; then
  echo "ERROR: jt9 binary not found or not executable: $JT9" >&2
  exit 2
fi
if [[ ! -f "$HARNESS" ]]; then
  echo "ERROR: contract harness not found: $HARNESS" >&2
  exit 2
fi

echo "external-producer streaming contract fixture"
echo "  jt9:       $JT9"
echo "  harness:   $HARNESS"

# <wav> <mode> <decode-floor>. FT8 + JT9 are the two external-producer-driven
# modes with a stable HF streaming fixture; both proven in run-streaming-fixtures.sh.
fixtures=(
  "samples/FT8/210703_133430.wav FT8 14"
  "samples/JT9/130418_1742.wav JT9 6"
)

fail=0
for entry in "${fixtures[@]}"; do
  read -r wav mode floor <<< "$entry"
  wav_path="$REPO_ROOT/$wav"
  if [[ ! -f "$wav_path" ]]; then
    echo "  SKIP: $wav (sample not present)"
    continue
  fi
  echo "  --- $mode ($wav) ---"
  if ! python3 "$HARNESS" "$JT9" "$wav_path" --mode "$mode" --floor "$floor"; then
    fail=1
  fi
done

if [[ "$fail" -eq 0 ]]; then
  echo "external-producer contract fixture passed (the minimal producer frame is accepted)."
else
  echo "external-producer contract fixture FAILED — the streaming wire contract has drifted." >&2
  exit 1
fi
