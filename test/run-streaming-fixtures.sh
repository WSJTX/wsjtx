#!/usr/bin/env bash
# Streaming-fixture regression harness (cross-mode validation).
#
# Pipes the same WAV fixtures used by run-golden-fixtures.sh through
# `jt9 --stream` via test/wav_to_stream_harness.py and asserts that the
# streaming-mode decode count meets or exceeds the WAV-mode ground truth.
#
# Streaming may produce MORE decodes than WAV mode (the early-pass
# nzhsym=41/47 calls can find low-SNR signals; not a regression). What we
# guard against is the "zero decodes" regression: any combination of
# params/protocol bugs that silences the FT8 decoder under --stream.
#
# Usage:
#   test/run-streaming-fixtures.sh [build-dir]
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

echo "Streaming-fixture regression"
echo "  jt9:       $JT9"
echo "  harness:   $HARNESS"

fail=0

# Format: <wav-path> <jt9-mode-name> <minimum-decode-count>
#
# Floors are set conservatively below WAV-mode ground truth to absorb
# small streaming/WAV variance (per file header). FT8 fixture: 14
# (streaming finds 21+; WAV finds 14). JT9: 6 (matches WAV). FT4:
# 14 (WAV=16, streaming=16). MSK144:
# 1 (WAV=1, streaming=1; the sample contains exactly one ping. The
# 181211_120800 sibling is a fallback if 181211_120500 ever produces 0).
# FST4: 2 (WAV=2, streaming=2; the 60s sample at
# 210115_0058 contains 'CQ K9KFR EN71' + 'CQ N5TM EL29').
# JT65 deliberately omitted — the JT65B capture
# samples/JT65/JT65B/000000_0001.wav decodes a -25 dB signal at JT65's
# ~-24 dB sensitivity floor. It is one of the upstream cad95e65c "test
# files for JT65 averaging, AP, and DS decodes" -- a weak-signal
# sequence. A probe of all 8 JT65B samples through this harness
# found ONLY 0001 decodes at all (others 0/6), and 0001 only on a
# quiescent box; the one non-averaging sample (DL7UAE) is 8-bit/11025 Hz
# which --stream hard-rejects (needs 16-bit/12 kHz), so no margin-clearing
# JT65 sample is available to swap in. On the single-pass streaming
# path (no WAV multi-pass threshold relaxation, jt65_decode.f90:122-145)
# the candidate sits on KVASD's dual accept gate (extract.f90:176), so a
# floor=1 HARD assertion is nondeterministic: ~50/50 quiescent, 0 under
# concurrent build/workflow churn. It turned CI RED on its first
# Linux-CI exposure: ubuntu-22.04 sampled 0, macos-14 sampled 1.
# Upstream itself gates NO JT65 decode (its tests/ are C++/Qt unit tests;
# the JT65B WAVs ship for manual GUI use only). nmode=65 dispatch/parse/
# apply coverage is retained via run-schema-v1-fixtures.sh (--mode JT65),
# the Phase-5 parse-unit, and the apply-routing unit; map65d JT65 wiring
# via run-eme-jt65-fixtures.sh. Same posture as FST4W below; replace-with-
# margin-sample and soft-gate were the rejected alternatives.
# Q65: 1 floor (streaming=4). 60s Q65-A sample; EME-sourced but
# decoded through the TERRESTRIAL jt9 --stream nmode=66 path (NOT map65d) —
# exactly the path an external streaming producer exercises. Closes the
# residual per-mode gap (FT4/MSK144/FST4 were closed earlier).
#
# FST4W deliberately omitted — the only available FST4W sample
# (samples/FST4+FST4W/201230_0300.wav, "FST4W-1800" per commit 97dce3aa7)
# does not decode under jt9 -W with any standard period (120/300/900/
# 1800s) on this build. Streaming-dispatch wiring still verified via
# the FST4W mode_string_to_int mapping (lib/streaming_control.f90:209)
# and TRperiod auto-default (streaming_io.f90:370 case default 60s);
# what's missing is a known-good FST4W decoder fixture.
fixtures=(
  "samples/FT8/210703_133430.wav FT8 14"
  "samples/JT9/130418_1742.wav JT9 6"
  "samples/FT4/000000_000002.wav FT4 14"
  "samples/MSK144/181211_120500.wav MSK144 1"
  "samples/FST4+FST4W/210115_0058.wav FST4 2"
  "samples/Q65/60A_EME_6m/210106_1621.wav Q65 1"
)

for entry in "${fixtures[@]}"; do
  read -r wav mode min_count <<< "$entry"
  wav_path="$REPO_ROOT/$wav"
  if [[ ! -f "$wav_path" ]]; then
    echo "  SKIP: $wav (sample not present)"
    continue
  fi

  count=$(python3 "$HARNESS" "$JT9" "$wav_path" --mode "$mode" --quiet 2>/dev/null \
    | grep -c '"t":"decode"' || true)
  if [[ "$count" -ge "$min_count" ]]; then
    echo "  PASS: $wav ($mode, $count decodes >= $min_count)"
  else
    echo "  FAIL: $wav ($mode, $count decodes < $min_count expected)"
    fail=1
  fi
done

if [[ "$fail" -eq 0 ]]; then
  echo "All streaming fixtures passed."
else
  echo "One or more streaming fixtures regressed."
  exit 1
fi
