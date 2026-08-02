#!/usr/bin/env bash
# golden-fixture regression harness.
#
# Compares `jt9` decoder output against known-good captures.
# Any regression that changes decoder stdout byte-for-byte fails here.
#
# Covers two fixtures (FT8 + JT9), and extends with
# FST4/Q65/MSK144 and map65d I/Q fixtures.
#
# Usage:
#   test/run-golden-fixtures.sh [build-dir]
#
# Where build-dir defaults to ./build and must contain a built jt9 binary.

set -euo pipefail

BUILD_DIR="${1:-build}"
REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
JT9="$REPO_ROOT/$BUILD_DIR/jt9"
SAMPLES="$REPO_ROOT/samples"
FIXTURES="$REPO_ROOT/test/fixtures"

if [[ ! -x "$JT9" ]]; then
  echo "ERROR: jt9 binary not found at $JT9" >&2
  echo "       Build it first: cmake --build $BUILD_DIR --target jt9" >&2
  exit 2
fi

FAIL=0
pass() { echo "  PASS: $1"; }
fail() { echo "  FAIL: $1"; FAIL=1; }

run_fixture() {
  local name="$1"; shift
  local sample="$1"; shift
  local expected="$1"; shift
  # Remaining args are jt9 flags (e.g., -8, -9, -7, etc.)

  local actual_file
  actual_file="$(mktemp -t "wsjtl-golden-$name.XXXXXX")"

  if ! "$JT9" "$@" "$sample" > "$actual_file" 2>&1; then
    fail "$name: jt9 exited non-zero"
    rm -f "$actual_file"
    return
  fi

  if diff -u "$expected" "$actual_file" > /dev/null 2>&1; then
    pass "$name"
  else
    fail "$name: output differs from golden (see diff below)"
    diff -u "$expected" "$actual_file" | head -30 | sed 's/^/    /'
  fi
  rm -f "$actual_file"
}

echo "golden-fixture regression"
echo "  jt9:       $JT9"
echo "  fixtures:  $FIXTURES"
echo ""

run_fixture "ft8_210703_133430" \
  "$SAMPLES/FT8/210703_133430.wav" \
  "$FIXTURES/ft8_210703_133430.expected.txt" \
  -8

run_fixture "jt9_130418_1742" \
  "$SAMPLES/JT9/130418_1742.wav" \
  "$FIXTURES/jt9_130418_1742.expected.txt" \
  -9

run_fixture "q65_30a_201203_024000" \
  "$SAMPLES/Q65/30A_Ionoscatter_6m/201203_024000.wav" \
  "$FIXTURES/q65_30a_201203_024000.expected.txt" \
  --q65 -p 30 -f 1000 -d 3

run_fixture "q65_300a_201210_0505" \
  "$SAMPLES/Q65/300A_Optical_Scatter/201210_0505.wav" \
  "$FIXTURES/q65_300a_201210_0505.expected.txt" \
  --q65 -p 300 -f 1000 -d 3

run_fixture "q65_60d_201212_1838" \
  "$SAMPLES/Q65/60D_EME_10GHz/201212_1838.wav" \
  "$FIXTURES/q65_60d_201212_1838.expected.txt" \
  --q65 -b D -p 60 -f 1000 -d 3

run_fixture "msk144_181211_120800" \
  "$SAMPLES/MSK144/181211_120800.wav" \
  "$FIXTURES/msk144_181211_120800.expected.txt" \
  --msk144 -p 15 -f 1500 -F 50 -d 3

run_fixture "fst4_210115_0058" \
  "$SAMPLES/FST4+FST4W/210115_0058.wav" \
  "$FIXTURES/fst4_210115_0058.expected.txt" \
  --fst4 -p 60 -f 1331 -L 1000 -H 1400 -d 3

run_fixture "fst4w_201230_0300" \
  "$SAMPLES/FST4+FST4W/201230_0300.wav" \
  "$FIXTURES/fst4w_201230_0300.expected.txt" \
  --fst4w -p 1800 -f 1433 -F 100 -d 3

run_fixture "ft4_000000_000002" \
  "$SAMPLES/FT4/000000_000002.wav" \
  "$FIXTURES/ft4_000000_000002.expected.txt" \
  --ft4 -d 3

run_fixture "jt65_000000_0004" \
  "$SAMPLES/JT65/JT65B/000000_0004.wav" \
  "$FIXTURES/jt65_000000_0004.expected.txt" \
  --jt65 -b B -f 1700 -d 3

run_fixture "jt4_OK1KIR_141105_175700" \
  "$SAMPLES/JT4/JT4F/OK1KIR_141105_175700.WAV" \
  "$FIXTURES/jt4_OK1KIR_141105_175700.expected.txt" \
  --jt4 -b F -f 1200 -d 3

echo ""
if [[ $FAIL -eq 0 ]]; then
  echo "All golden fixtures passed."
  exit 0
else
  echo "One or more golden fixtures regressed."
  exit 1
fi
