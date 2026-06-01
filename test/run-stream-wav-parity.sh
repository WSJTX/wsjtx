#!/usr/bin/env bash
# Stream-vs-WAV decode-parity harness.
#
# Asserts CONTAINMENT: every signal in a committed WAV-mode golden
# (test/fixtures/*.expected.txt) is also decoded by `jt9 --stream`. Streaming
# legitimately finds MORE (the early nzhsym passes pick up low-SNR signals; see
# run-streaming-fixtures.sh) so the relation is "WAV goldens are a subset of
# stream decodes", NOT equality. This is the rigorous regression guard the
# decode-affecting later phases (8, 9) depend on.
#
# Identity is the decoded MESSAGE text. Empirically snr/dt/freq drift between
# the WAV and stream decode paths for the SAME signal (snr ±2 dB, dt ±0.05 s,
# freq ±3 Hz on these fixtures — the streaming early passes compute slightly
# different fine estimates), so the harness shows them as diagnostics but the
# pass/fail hinges only on message-set containment.
#
# Scope: FT8 and JT9 ONLY (stated, not silently capped). FST4W has no
# known-good decoder fixture and the EME/JT65 I/Q paths are
# pass-on-absence, so neither can anchor a parity golden.
#
# A NEGATIVE CONTROL gate proves the harness can fail: a golden carrying one
# synthetic impossible signal MUST be reported as a parity failure. Without it,
# a green run cannot distinguish "containment held" from "harness never fails".
#
# Usage:
#   test/run-stream-wav-parity.sh [build-dir]
#
# Where build-dir defaults to ./build and must contain a built jt9 binary.

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

echo "Stream-vs-WAV decode-parity (containment; FT8/JT9 only)"
echo "  jt9:     $JT9"
echo "  harness: $HARNESS"

fail=0

# Format: <wav-path> <jt9-mode-name> <golden-path>
fixtures=(
  "samples/FT8/210703_133430.wav FT8 test/fixtures/ft8_210703_133430.expected.txt"
  "samples/JT9/130418_1742.wav JT9 test/fixtures/jt9_130418_1742.expected.txt"
)

for entry in "${fixtures[@]}"; do
  read -r wav mode golden <<< "$entry"
  wav_path="$REPO_ROOT/$wav"
  golden_path="$REPO_ROOT/$golden"
  echo ""
  if [[ ! -f "$wav_path" ]]; then
    echo "  SKIP: $wav (sample not present)"
    continue
  fi
  if [[ ! -f "$golden_path" ]]; then
    echo "  SKIP: $golden (golden not present)"
    continue
  fi
  if python3 "$HARNESS" "$JT9" "$wav_path" --mode "$mode" --parity "$golden_path"; then
    echo "  PASS: $mode parity (WAV goldens are a subset of stream decodes)"
  else
    echo "  FAIL: $mode parity — a WAV-confirmed signal was dropped"
    fail=1
  fi
done

# --- Negative control: prove the harness can FAIL ---------------------------
echo ""
echo "Negative control — a golden with one impossible signal MUST fail parity"
ng_wav="$REPO_ROOT/samples/FT8/210703_133430.wav"
ng_src="$REPO_ROOT/test/fixtures/ft8_210703_133430.expected.txt"
if [[ -f "$ng_wav" && -f "$ng_src" ]]; then
  tmp="$(mktemp)"
  trap 'rm -f "$tmp"' EXIT
  cat "$ng_src" > "$tmp"
  # A syntactically valid WSJT-X decode line whose message this audio can
  # never produce. The parser must report it as a dropped (MISSING) signal.
  printf '133430  -5  0.0 1500 ~  ZZ9ZZZ NOTREAL XX99\n' >> "$tmp"
  if python3 "$HARNESS" "$JT9" "$ng_wav" --mode FT8 --parity "$tmp" >/dev/null 2>&1; then
    echo "  FAIL: harness reported parity OK on a golden with an impossible signal (cannot discriminate)"
    fail=1
  else
    echo "  PASS: harness correctly flagged the impossible signal as dropped"
  fi
  rm -f "$tmp"
  trap - EXIT

  # An empty/truncated golden must NOT pass vacuously (0 signals ⊆ anything is
  # trivially true). The harness must fail it loudly so a corrupt baseline is
  # caught rather than masking future regressions.
  echo ""
  echo "Negative control 2 — an EMPTY golden MUST fail parity (no vacuous pass)"
  empty="$(mktemp)"
  trap 'rm -f "$empty"' EXIT
  : > "$empty"   # zero-byte golden
  if python3 "$HARNESS" "$JT9" "$ng_wav" --mode FT8 --parity "$empty" >/dev/null 2>&1; then
    echo "  FAIL: harness reported parity OK on an empty golden (vacuous pass)"
    fail=1
  else
    echo "  PASS: harness correctly rejected the empty golden"
  fi
  rm -f "$empty"
  trap - EXIT
else
  echo "  SKIP: FT8 sample/golden not present for the negative control"
fi

echo ""
if [[ "$fail" -eq 0 ]]; then
  echo "All stream-vs-WAV parity checks passed."
else
  echo "One or more stream-vs-WAV parity checks FAILED."
  exit 1
fi
