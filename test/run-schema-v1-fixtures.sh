#!/usr/bin/env bash
# Streaming schema-v1 regression — Phases 1 + 2 + 4 + 5 + 6 + 7 + 8 + 9
# (schema versioning, structured error envelope, per-key type validation + atomic
# decline, the Phase 4 operator-identity / frequency-window field group, the
# Phase 5 decoder-tuning field group, the Phase 6 FT8/FT4 specifics cluster, the
# Phase 7 diagnostic / sequencing field group, the Phase 8 string / scaled /
# derived field group, and the Phase 9 bit-packed ndepth / nexp_decode fields).
#
# Twenty-six gates:
#   1. Unknown schema version: a configure with version=2 is declined with
#      exactly one {"t":"error","code":"unknown_schema_version","got":2}.
#   2. Explicit version=1 accepted: configure applies normally; FT8 decodes
#      >= floor and NO unknown_schema_version is emitted.
#   3. Malformed control frame (missing "t"): emits
#      {"t":"error","code":"configure_parse_error",...} AND the stream
#      continues (the following valid configure applies, FT8 decodes >= floor).
#   4. (Phase 2) Per-key TYPE error + whole-frame decline: a configure with a
#      wrong-typed key (depth as a string) AND a co-present good key (mode=JT65)
#      emits exactly one {"code":"configure_type_error","key":"depth",
#      "expected":"int","got":"string"} and the good key does NOT apply — the
#      FT8 audio still decodes under the FT8 init default (>= floor), proving the
#      ENTIRE frame was declined (no partial application).
#   5. (Phase 2) Recovery: a wrong-typed frame is declined with
#      configure_type_error, then the following VALID configure applies normally.
#      Proven by an APPLIED side-effect of the recovered frame (its nutc=133430
#      appears in the decode "time" fields) — a count floor alone cannot tell a
#      recovered frame (21 decodes) from a silently-dropped one (14, init
#      default), but the nutc reflection can (dropped -> time 000000).
#   6. (Phase 2) Value-vs-key aliasing guard: a configure whose mycall VALUE is
#      the literal token "mygrid" (with no real mygrid key) must be ACCEPTED, not
#      falsely declined (type validation must anchor keys to structural
#      positions, not match a quoted value).
#   7. (Phase 4) All 9 operator-identity / frequency-window keys present with
#      correct types (his_call/his_grid/my_b_call/his_b_call strings,
#      my_call_standard/his_call_standard bools, tx_audio_offset_hz/
#      jt65_jt9_split_hz/max_drift_hz ints) are ACCEPTED end-to-end: ZERO
#      configure_type_error and FT8 still decodes >= floor (parse + apply run
#      without corrupting params). These fields are no-ops on the FT8 fixture
#      (tx/Q65/split-only), so this gate proves the path, not a decode effect.
#   8. (Phase 4) A wrong-typed Phase 4 key (my_call_standard as a NUMBER) on a
#      frame that also sets mode=JT65 emits exactly one {"code":
#      "configure_type_error","key":"my_call_standard","expected":"bool",
#      "got":"number"} and the whole frame is declined — mode JT65 does NOT
#      apply, so the FT8 audio still decodes under the FT8 init default
#      (>= floor). Proves Phase 4 type checks are wired into atomic-decline.
#   9. (Phase 5) All 12 decoder-tuning keys (dt_tolerance_seconds/
#      eme_delay_seconds reals, kin_samples/nzhsym_per_period/npts_c0_array/
#      min_width/min_sync/n_2pass ints, robust_mode/nagain_flag/clear_average
#      bools, tx_mode enum) present with correct types are ACCEPTED end-to-end:
#      ZERO configure_type_error and FT8 still decodes >= floor (parse + apply
#      run without corrupting params). 10 of 12 are inert on FT8 (per-period
#      recompute / JT65-Q65-only); this gate proves the PATH, not per-key routing
#      (that is the parse-unit driver + Gates 11/12).
#  10. (Phase 5) A wrong-typed Phase 5 key (dt_tolerance_seconds as a STRING) on
#      a frame that also sets mode=JT65 emits exactly one {"code":
#      "configure_type_error","key":"dt_tolerance_seconds","expected":"real",
#      "got":"string"} and the whole frame is declined (mode JT65 not applied,
#      FT8 >= floor). Proves Phase 5 type checks are wired into atomic-decline.
#  11. (Phase 5 — APPLY ROUTING) nagain_flag=true changes the FT8 decode
#      message-set vs the no-key baseline (empirically 21 -> 16). nagain is
#      the ONLY Phase 5 key that reaches the FT8 decoder, so an observable change
#      proves nagain_flag was applied to an FT8-affecting param — a partial
#      apply-routing guard the inert Phase 4 fields could not have.
#  12. (Phase 5 — APPLY ROUTING) npts_c0_array=60000 changes the JT9 decode
#      message-set vs the no-key baseline (empirically 7 -> 6). npts8 is
#      the Phase 5 key that reaches the JT9 decoder, so an observable change
#      proves npts_c0_array was applied to a JT9-affecting param.
#  13. (Phase 6) All 24 FT8/FT4-specifics keys present with correct types are
#      ACCEPTED end-to-end: ZERO configure_type_error and FT8 still decodes
#      >= floor. 23 of 24 are inert on the FT8 fixture (read only in the
#      multithread-FT8 block / JT65 / superfox-Q65 / WAV path; or AP keys that
#      this fixture has no AP-only decodes for); the 24th,
#      multithreaded_ft8, is sent TRUE here and only stays >= floor BECAUSE the
#      apply ignores the non-false value (Gate 15). So this proves the parse->apply
#      PATH; per-key routing is guarded by the parse-unit + apply-routing units.
#  14. (Phase 6) A wrong-typed Phase 6 key (ft8_ap_on as a NUMBER) on a frame that
#      also sets mode=JT65 emits exactly one {"code":"configure_type_error",
#      "key":"ft8_ap_on","expected":"bool","got":"number"} and the whole frame is
#      declined (mode JT65 not applied, FT8 >= floor). Wires Phase 6 type checks
#      into atomic-decline.
#  15. (Phase 6 — CORRECTNESS GUARD) multithreaded_ft8=true must STILL decode FT8
#      >= floor. Setting lmultift8=.true. would take the decoder.f90:192 multithread
#      branch, which yields 0 FT8 decodes on the stream path; the apply IGNORES
#      the non-false value (the receiver ignores a non-false value). A regression
#      to a straight pass-through would collapse FT8 to 0 and fail here.
#  16. (Phase 7) All 6 diagnostic / sequencing keys (last_tx_seconds_ago/
#      qso_progress_state/sec_band_changed/delay_units ints, currently_txing/
#      mode_changed bools) present with correct types are ACCEPTED end-to-end:
#      ZERO configure_type_error and FT8 still decodes >= floor (parse + apply run
#      without corrupting params). All 6 are inert on the FT8 fixture (read only
#      in the multithread-FT8 block / AP passes with no AP-only decodes),
#      so this gate proves the PATH, not a decode effect; per-key routing is
#      guarded by run-phase7-parse-unit.sh + run-apply-routing-unit.sh.
#  17. (Phase 7) A wrong-typed Phase 7 key (last_tx_seconds_ago as a STRING) on a
#      frame that also sets mode=JT65 emits exactly one {"code":
#      "configure_type_error","key":"last_tx_seconds_ago","expected":"int",
#      "got":"string"} and the whole frame is declined (mode JT65 not applied,
#      FT8 >= floor). Wires Phase 7 type checks into atomic-decline.
#  18. (Phase 7 — CORRECTNESS GUARD) qso_progress_state OUT OF RANGE must STILL
#      decode FT8 >= floor. nQSOProgress reaches the single-pass FT8 decoder
#      (decoder.f90:1141), where ft8b.f90:274/299 use it as a raw index into
#      nappasses(0:5)/naptypes(0:5,4); an out-of-range value (here 99) faults the
#      decoder under -fbounds-check (0 decodes). The apply IGNORES
#      an out-of-range value (valid [0,5] only), so FT8 must STILL decode >= floor.
#      A regression to a straight pass-through would crash and fail here.
#  19. (Phase 8 — APPLY ROUTING) All 5 string/scaled/derived keys (utc/date/
#      n_trials/candthin_threshold/dt_center_seconds) accepted with 0 type errors,
#      AND utc:"13:34:30" -> nutc 133430 is reflected in EVERY decode's "time"
#      field (utc is decode-observable; the other 4 are inert — superfox / JT65 /
#      multithread-variant only). A real apply-routing guard for utc.
#  20. (Phase 8 — PRECEDENCE) A frame with BOTH nutc:120000 and utc:"13:34:30"
#      applies utc (time 133430), not nutc (120000) — apply order is nutc then utc
#      (utc wins). Asserts 133430 present AND 120000 absent.
#  21. (Phase 8 — VALUE VALIDATION) n_trials=500 (a number, but not 10^N or 3*10^N)
#      emits one configure_type_error key:"n_trials" and declines the whole frame
#      (co-present mode=JT65 does not apply; FT8 decodes under its default).
#      A non-conforming value would integer-divide into a wrong ntrials.
#  22. (Phase 8 — VALUE VALIDATION) date=2200-01-01 (year >= 2100) emits one
#      configure_type_error key:"date" and declines the whole frame.
#      The 2000+offset YYMMDD encoding caps at 2099.
#  23. (Phase 9 — ACCEPTED) All 9 unpacked ndepth/nexp_decode keys accepted with 0
#      type errors and FT8 >= floor (depth_level=3/q65_maxiters_level=3 keep ndepth=3
#      so the count is unperturbed; the other 7 are inert on FT8 — Q65/JT65/FST4/JT4
#      or contest-branch only). Proves the parse->pack->apply path runs clean.
#  24. (Phase 9 — APPLY ROUTING) depth_level is decode-observable: the FT8 fixture
#      runs at depth=3 -> ndepth=3 (21 decodes); depth_level=1 -> ndepth=1 changes
#      the FT8 pass structure (ft8_decode.f90:178/182) -> 14 decodes. Assertion:
#      baseline >= floor AND with-key >= floor AND with-key != baseline (both-sides-
#      healthy differs guard — a crash/empty with-key also "differs", so guard it).
#      A real apply-routing guard for the one FT8-observable Phase-9 key.
#  25. (Phase 9 — CONFLICT DECLINE) A frame carrying BOTH the legacy depth and the
#      unpacked depth_level emits one configure_type_error key:"ndepth"
#      expected:"depth|unpacked" got:"conflict" and declines the whole frame
#      (co-present mode=JT65 does not apply; FT8 decodes under its default). The
#      conflicting-encodings rule, via the parse post-pass pack_phase9_.
#  26. (Phase 9 — TYPE DECLINE) A wrong-typed Phase 9 key (depth_level as a string)
#      emits one configure_type_error key:"depth_level" expected:"int" got:"string"
#      and declines the whole frame (mode=JT65 not applied; FT8 >= floor). Wires
#      Phase 9 type checks into atomic-decline.
#
# Usage:
#   test/run-schema-v1-fixtures.sh [build-dir]

set -euo pipefail

BUILD_DIR="${1:-build}"
REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
JT9="$REPO_ROOT/$BUILD_DIR/jt9"
HARNESS="$REPO_ROOT/test/wav_to_stream_harness.py"
WAV="$REPO_ROOT/samples/FT8/210703_133430.wav"
JT9_WAV="$REPO_ROOT/samples/JT9/130418_1742.wav"
FT8_FLOOR=14
JT9_FLOOR=6

if [[ ! -x "$JT9" ]]; then
  echo "ERROR: jt9 binary not found or not executable: $JT9" >&2
  exit 2
fi
if [[ ! -f "$HARNESS" ]]; then
  echo "ERROR: harness not found: $HARNESS" >&2
  exit 2
fi

echo "Streaming schema-v1 regression (Phase 1: versioning + structured errors)"
echo "  jt9:       $JT9"
echo "  harness:   $HARNESS"

fail=0

# --- Gate 1: unknown schema version declined --------------------------------
echo ""
echo "Gate 1 — unknown schema version (version=2 declined)"
out=$(python3 "$HARNESS" "$JT9" "$WAV" --mode FT8 --schema-version 2 --quiet 2>/dev/null || true)
uv_count=$(printf '%s\n' "$out" | grep -c '"code":"unknown_schema_version"' || true)
got_count=$(printf '%s\n' "$out" | grep -c '"got":2' || true)
if [[ "$uv_count" -eq 1 && "$got_count" -ge 1 ]]; then
  echo "  PASS: version=2 declined with exactly one unknown_schema_version (got:2)"
else
  echo "  FAIL: expected exactly 1 unknown_schema_version with got:2 (saw code=$uv_count got=$got_count)"
  fail=1
fi

# --- Gate 2: explicit version=1 accepted ------------------------------------
echo ""
echo "Gate 2 — explicit version=1 accepted (normal decode)"
out=$(python3 "$HARNESS" "$JT9" "$WAV" --mode FT8 --schema-version 1 --quiet 2>/dev/null || true)
dec=$(printf '%s\n' "$out" | grep -c '"t":"decode"' || true)
uv=$(printf '%s\n' "$out" | grep -c '"code":"unknown_schema_version"' || true)
if [[ "$dec" -ge "$FT8_FLOOR" && "$uv" -eq 0 ]]; then
  echo "  PASS: version=1 accepted; $dec decodes >= $FT8_FLOOR, no version error"
else
  echo "  FAIL: version=1 — $dec decodes (expected >= $FT8_FLOOR), $uv version errors (expected 0)"
  fail=1
fi

# --- Gate 3: malformed control frame -> configure_parse_error ---------------
echo ""
echo "Gate 3 — malformed control frame (configure_parse_error + graceful continue)"
out=$(python3 "$HARNESS" "$JT9" "$WAV" --mode FT8 --bad-control --quiet 2>/dev/null || true)
pe=$(printf '%s\n' "$out" | grep -c '"code":"configure_parse_error"' || true)
dec=$(printf '%s\n' "$out" | grep -c '"t":"decode"' || true)
if [[ "$pe" -ge 1 && "$dec" -ge "$FT8_FLOOR" ]]; then
  echo "  PASS: configure_parse_error emitted ($pe) and stream continued ($dec decodes >= $FT8_FLOOR)"
else
  echo "  FAIL: parse-error=$pe (expected >= 1), decodes=$dec (expected >= $FT8_FLOOR)"
  fail=1
fi

# --- Gate 4: per-key type error + atomic decline (no partial application) ---
echo ""
echo "Gate 4 — configure_type_error + whole-frame decline (no partial application)"
# MAIN configure is wrong-typed (depth as a JSON string) AND sets mode JT65.
# Atomic decline: the entire frame -> mode JT65 must NOT apply, so the FT8
# audio decodes under the FT8 init default (>= floor). Partial application
# (mode=JT65) would collapse FT8 decodes to ~0.
out=$(python3 "$HARNESS" "$JT9" "$WAV" --mode JT65 --bad-type --quiet 2>/dev/null || true)
te=$(printf '%s\n' "$out" | grep -c '"code":"configure_type_error"' || true)
triple=$(printf '%s\n' "$out" | grep -c '"key":"depth","expected":"int","got":"string"' || true)
dec=$(printf '%s\n' "$out" | grep -c '"t":"decode"' || true)
if [[ "$te" -eq 1 && "$triple" -ge 1 && "$dec" -ge "$FT8_FLOOR" ]]; then
  echo "  PASS: one configure_type_error (depth/int/string); whole frame declined, mode JT65 not applied ($dec FT8 decodes >= $FT8_FLOOR)"
else
  echo "  FAIL: type_err=$te (want 1), key/expected/got match=$triple (want >=1), decodes=$dec (want >= $FT8_FLOOR)"
  fail=1
fi

# --- Gate 5: type error then recovery (recovered frame's effect must apply) --
echo ""
echo "Gate 5 — type error then recovery (recovered frame's nutc must be applied)"
# Declined bad-type preamble, then a VALID main configure carrying nutc=133430.
# Recovery is proven by the APPLIED side-effect (decode time == 133430), not by
# the decode count: a silently-dropped recovered frame would still yield ~14
# init-default decodes (>= FT8_FLOOR) but with time 000000, which this gate
# rejects.
out=$(python3 "$HARNESS" "$JT9" "$WAV" --mode FT8 --bad-type-preamble --nutc 133430 --quiet 2>/dev/null || true)
te=$(printf '%s\n' "$out" | grep -c '"code":"configure_type_error"' || true)
dec=$(printf '%s\n' "$out" | grep -c '"t":"decode"' || true)
rec=$(printf '%s\n' "$out" | grep '"t":"decode"' | grep -c '"time":"133430"' || true)
if [[ "$te" -ge 1 && "$dec" -ge "$FT8_FLOOR" && "$rec" -ge 1 ]]; then
  echo "  PASS: configure_type_error emitted ($te); recovered frame APPLIED ($rec/$dec decodes carry nutc 133430)"
else
  echo "  FAIL: type_err=$te (want >=1), decodes=$dec (want >= $FT8_FLOOR), nutc-reflected=$rec (want >=1)"
  fail=1
fi

# --- Gate 6: value-vs-key aliasing must NOT falsely decline (regression) -----
echo ""
echo "Gate 6 — value-vs-key aliasing accepted (mycall value == \"mygrid\" token)"
# {"t":"configure","mode":"FT8","depth":3,"mycall":"mygrid"} (no real mygrid key).
# A well-typed string value that equals another key's token must NOT be mistaken
# for that key and falsely declined. Expect ZERO configure_type_error and a
# normal decode. (wire-parity regression guard)
out=$(python3 "$HARNESS" "$JT9" "$WAV" --mode FT8 --mycall mygrid --omit-mygrid --quiet 2>/dev/null || true)
te=$(printf '%s\n' "$out" | grep -c '"code":"configure_type_error"' || true)
dec=$(printf '%s\n' "$out" | grep -c '"t":"decode"' || true)
if [[ "$te" -eq 0 && "$dec" -ge "$FT8_FLOOR" ]]; then
  echo "  PASS: aliasing frame accepted (0 type errors); $dec decodes >= $FT8_FLOOR"
else
  echo "  FAIL: type_err=$te (want 0 — false decline = regression), decodes=$dec (want >= $FT8_FLOOR)"
  fail=1
fi

# --- Gate 7: Phase 4 — all 9 keys accepted (parse + apply run clean) ---------
echo ""
echo "Gate 7 — Phase 4 fields accepted (his_call/his_grid/my_b_call/his_b_call/"
echo "         my_call_standard/his_call_standard/tx_audio_offset_hz/"
echo "         jt65_jt9_split_hz/max_drift_hz)"
# All 9 keys present with correct types and benign values. They are no-ops on
# the FT8 fixture (tx/Q65/split-only — an all-9-keys configure yields a
# byte-identical 21-message FT8 decode set), so the assertion is: ZERO type
# errors (every key accepted) AND FT8 still decodes >= floor (apply ran without
# corrupting params). This proves the parse->apply PATH executes cleanly; it
# does NOT prove each value lands in its CORRECT params slot (the fields are
# inert here, so a mis-route would still pass) — that routing is guarded by the
# parse-unit driver (test/run-phase4-parse-unit.sh, parse side) + code review.
out=$(python3 "$HARNESS" "$JT9" "$WAV" --mode FT8 \
      --his-call DX1ABC --his-grid FN20 --my-b-call KJ5HST/P --his-b-call DX1ABC/P \
      --my-call-standard --his-call-standard \
      --tx-audio-offset-hz 1234 --jt65-jt9-split-hz 2500 --max-drift-hz 8 \
      --quiet 2>/dev/null || true)
te=$(printf '%s\n' "$out" | grep -c '"code":"configure_type_error"' || true)
dec=$(printf '%s\n' "$out" | grep -c '"t":"decode"' || true)
if [[ "$te" -eq 0 && "$dec" -ge "$FT8_FLOOR" ]]; then
  echo "  PASS: all 9 Phase 4 keys accepted (0 type errors); $dec decodes >= $FT8_FLOOR (apply clean)"
else
  echo "  FAIL: type_err=$te (want 0), decodes=$dec (want >= $FT8_FLOOR)"
  fail=1
fi

# --- Gate 8: Phase 4 — wrong-typed key -> type error + atomic decline --------
echo ""
echo "Gate 8 — Phase 4 wrong-typed key (my_call_standard as number) declines frame"
# MAIN configure carries my_call_standard:5 (number, wrong type for a bool) AND
# sets mode=JT65. Atomic decline: the entire frame -> mode JT65 must NOT
# apply, so the FT8 audio decodes under the FT8 init default (>= floor). The
# error must name the Phase 4 key with expected:"bool" got:"number".
out=$(python3 "$HARNESS" "$JT9" "$WAV" --mode JT65 --bad-phase4 --quiet 2>/dev/null || true)
te=$(printf '%s\n' "$out" | grep -c '"code":"configure_type_error"' || true)
triple=$(printf '%s\n' "$out" | grep -c '"key":"my_call_standard","expected":"bool","got":"number"' || true)
dec=$(printf '%s\n' "$out" | grep -c '"t":"decode"' || true)
if [[ "$te" -eq 1 && "$triple" -ge 1 && "$dec" -ge "$FT8_FLOOR" ]]; then
  echo "  PASS: one configure_type_error (my_call_standard/bool/number); frame declined, mode JT65 not applied ($dec FT8 decodes >= $FT8_FLOOR)"
else
  echo "  FAIL: type_err=$te (want 1), key/expected/got match=$triple (want >=1), decodes=$dec (want >= $FT8_FLOOR)"
  fail=1
fi

# --- Gate 9: Phase 5 — all 12 keys accepted (parse + apply run clean) --------
echo ""
echo "Gate 9 — Phase 5 fields accepted (dt_tolerance_seconds/eme_delay_seconds/"
echo "         kin_samples/nzhsym_per_period/npts_c0_array/min_width/min_sync/"
echo "         n_2pass/robust_mode/nagain_flag/tx_mode/clear_average)"
# All 12 keys present with correct types and benign values. 10 are inert on FT8
# (per-period recompute or JT65/Q65-only); nagain_flag=true does affect FT8
# (21->16, still >= floor) and npts_c0_array is set to its streaming default
# (74736, a no-op). Assertion: ZERO type errors (every key accepted) AND FT8 still
# decodes >= floor (apply ran without corrupting params). Proves the parse->apply
# PATH; per-key routing is guarded by run-phase5-parse-unit.sh + Gates 11/12.
out=$(python3 "$HARNESS" "$JT9" "$WAV" --mode FT8 \
      --dt-tolerance-seconds 2.0 --eme-delay-seconds 0.0 --kin-samples 64800 \
      --nzhsym-per-period 50 --npts-c0-array 74736 --min-width 1 --min-sync 1 \
      --n-2pass 1 --robust-mode --nagain-flag --tx-mode JT9 --clear-average \
      --quiet 2>/dev/null || true)
te=$(printf '%s\n' "$out" | grep -c '"code":"configure_type_error"' || true)
dec=$(printf '%s\n' "$out" | grep -c '"t":"decode"' || true)
if [[ "$te" -eq 0 && "$dec" -ge "$FT8_FLOOR" ]]; then
  echo "  PASS: all 12 Phase 5 keys accepted (0 type errors); $dec decodes >= $FT8_FLOOR (apply clean)"
else
  echo "  FAIL: type_err=$te (want 0), decodes=$dec (want >= $FT8_FLOOR)"
  fail=1
fi

# --- Gate 10: Phase 5 — wrong-typed key -> type error + atomic decline --------
echo ""
echo "Gate 10 — Phase 5 wrong-typed key (dt_tolerance_seconds as string) declines frame"
# MAIN configure carries dt_tolerance_seconds:"abc" (string, wrong type for a
# real) AND sets mode=JT65. Atomic decline: the entire frame -> mode JT65 must
# NOT apply, so FT8 decodes under the FT8 init default (>= floor). The error must
# name the Phase 5 key with expected:"real" got:"string".
out=$(python3 "$HARNESS" "$JT9" "$WAV" --mode JT65 --bad-phase5 --quiet 2>/dev/null || true)
te=$(printf '%s\n' "$out" | grep -c '"code":"configure_type_error"' || true)
triple=$(printf '%s\n' "$out" | grep -c '"key":"dt_tolerance_seconds","expected":"real","got":"string"' || true)
dec=$(printf '%s\n' "$out" | grep -c '"t":"decode"' || true)
if [[ "$te" -eq 1 && "$triple" -ge 1 && "$dec" -ge "$FT8_FLOOR" ]]; then
  echo "  PASS: one configure_type_error (dt_tolerance_seconds/real/string); frame declined, mode JT65 not applied ($dec FT8 decodes >= $FT8_FLOOR)"
else
  echo "  FAIL: type_err=$te (want 1), key/expected/got match=$triple (want >=1), decodes=$dec (want >= $FT8_FLOOR)"
  fail=1
fi

# --- Gate 11: Phase 5 APPLY ROUTING — nagain_flag observably changes FT8 ------
echo ""
echo "Gate 11 — nagain_flag=true changes the FT8 decode set (apply-routing proof)"
# nagain is the only Phase 5 key reaching the FT8 decoder (ft8_decode.f90 final
# pass). Baseline (no key) vs --nagain-flag must DIFFER, proving apply routed
# nagain_flag to an FT8-affecting param. Baseline computed fresh (robust to
# count drift); guard against a vacuous pass by requiring a non-empty baseline.
base=$(python3 "$HARNESS" "$JT9" "$WAV" --mode FT8 --quiet 2>/dev/null \
       | grep '"t":"decode"' | grep -oE '"message":"[^"]*"' | sort || true)
withk=$(python3 "$HARNESS" "$JT9" "$WAV" --mode FT8 --nagain-flag --quiet 2>/dev/null \
        | grep '"t":"decode"' | grep -oE '"message":"[^"]*"' | sort || true)
nbase=$(printf '%s\n' "$base" | grep -c '"message"' || true)
nwith=$(printf '%s\n' "$withk" | grep -c '"message"' || true)
# Guard BOTH sides: a crash / shmem-collision that emits nothing yields withk=""
# which "differs" from a healthy baseline and would FALSELY pass. Require the
# with-key run to be a healthy decode (>= 1 msg) too — only then does "differs"
# prove the key reached the decoder (not that one side died). (guards a vacuous pass)
if [[ -n "$base" && "$nbase" -ge "$FT8_FLOOR" && "$nwith" -ge 1 && "$base" != "$withk" ]]; then
  echo "  PASS: nagain_flag observably changed FT8 decode set (baseline $nbase msgs != with-key $nwith msgs)"
else
  echo "  FAIL: nagain_flag effect unproven (baseline=$nbase >= $FT8_FLOOR?, with-key=$nwith >= 1?, sets differ=$([[ "$base" != "$withk" ]] && echo yes || echo no))"
  fail=1
fi

# --- Gate 12: Phase 5 APPLY ROUTING — npts_c0_array observably changes JT9 ----
echo ""
echo "Gate 12 — npts_c0_array changes the JT9 decode set (apply-routing proof)"
# npts8 reaches the JT9 decoder (decoder.f90:1349). Baseline (no key) vs
# --npts-c0-array 60000 (off the 74736 default) must DIFFER, proving apply routed
# npts_c0_array to a JT9-affecting param. Uses the JT9 fixture; baseline fresh.
if [[ -f "$JT9_WAV" ]]; then
  base=$(python3 "$HARNESS" "$JT9" "$JT9_WAV" --mode JT9 --quiet 2>/dev/null \
         | grep '"t":"decode"' | grep -oE '"message":"[^"]*"' | sort || true)
  withk=$(python3 "$HARNESS" "$JT9" "$JT9_WAV" --mode JT9 --npts-c0-array 60000 --quiet 2>/dev/null \
          | grep '"t":"decode"' | grep -oE '"message":"[^"]*"' | sort || true)
  nbase=$(printf '%s\n' "$base" | grep -c '"message"' || true)
  nwith=$(printf '%s\n' "$withk" | grep -c '"message"' || true)
  # Same both-sides guard as Gate 11 (crash / shmem-collision -> empty withk
  # must NOT pass as a spurious "difference").
  if [[ -n "$base" && "$nbase" -ge "$JT9_FLOOR" && "$nwith" -ge 1 && "$base" != "$withk" ]]; then
    echo "  PASS: npts_c0_array observably changed JT9 decode set (baseline $nbase msgs != with-key $nwith msgs)"
  else
    echo "  FAIL: npts_c0_array effect unproven (baseline=$nbase >= $JT9_FLOOR?, with-key=$nwith >= 1?, sets differ=$([[ "$base" != "$withk" ]] && echo yes || echo no))"
    fail=1
  fi
else
  echo "  SKIP: JT9 fixture not found ($JT9_WAV)"
fi

# --- Gate 13: Phase 6 — all 24 keys accepted (parse + apply run clean) --------
echo ""
echo "Gate 13 — Phase 6 FT8/FT4-specifics keys accepted (24 keys: 19 bools + 5 ints)"
# All 24 keys present with correct types and benign values. 23 are inert on the
# FT8 fixture (multithread-block / JT65 / superfox / WAV-path / AP-no-effect).
# multithreaded_ft8 is sent TRUE and only stays >= floor because
# the apply ignores the non-false value (Gate 15). Assertion: ZERO type errors
# (every key accepted) AND FT8 still decodes >= floor (apply ran without
# corrupting params). Per-key routing is guarded by run-phase6-parse-unit.sh +
# run-apply-routing-unit.sh (no Phase 6 key is decode-observable in a healthy way).
out=$(python3 "$HARNESS" "$JT9" "$WAV" --mode FT8 \
      --multithreaded-ft8 --ft8-cycles 3 --ft8-rxf-sensitivity 3 --ft8-threads 0 \
      --ft8-decoder-start 3 --ft8-low-threshold --ft8-subpass --ft8-ap-on \
      --ap-cq-only --ap-my-call --jt65-ap-on --ap-width-hz 150 \
      --hide-ft8-duplicates --common-ft8b --enable-dxc-search --wide-dxc-search \
      --superfox-mode --even-sequence --hound-mode --multi-instance --skip-tx1 \
      --nagain-filter --stop-hint --hint-mode \
      --quiet 2>/dev/null || true)
te=$(printf '%s\n' "$out" | grep -c '"code":"configure_type_error"' || true)
dec=$(printf '%s\n' "$out" | grep -c '"t":"decode"' || true)
if [[ "$te" -eq 0 && "$dec" -ge "$FT8_FLOOR" ]]; then
  echo "  PASS: all 24 Phase 6 keys accepted (0 type errors); $dec decodes >= $FT8_FLOOR (apply clean)"
else
  echo "  FAIL: type_err=$te (want 0), decodes=$dec (want >= $FT8_FLOOR)"
  fail=1
fi

# --- Gate 14: Phase 6 — wrong-typed key -> type error + atomic decline --------
echo ""
echo "Gate 14 — Phase 6 wrong-typed key (ft8_ap_on as number) declines frame"
# MAIN configure carries ft8_ap_on:5 (number, wrong type for a bool) AND sets
# mode=JT65. Atomic decline: the entire frame -> mode JT65 must NOT apply, so the
# FT8 audio decodes under the FT8 init default (>= floor). The error must name the
# Phase 6 key with expected:"bool" got:"number".
out=$(python3 "$HARNESS" "$JT9" "$WAV" --mode JT65 --bad-phase6 --quiet 2>/dev/null || true)
te=$(printf '%s\n' "$out" | grep -c '"code":"configure_type_error"' || true)
triple=$(printf '%s\n' "$out" | grep -c '"key":"ft8_ap_on","expected":"bool","got":"number"' || true)
dec=$(printf '%s\n' "$out" | grep -c '"t":"decode"' || true)
if [[ "$te" -eq 1 && "$triple" -ge 1 && "$dec" -ge "$FT8_FLOOR" ]]; then
  echo "  PASS: one configure_type_error (ft8_ap_on/bool/number); frame declined, mode JT65 not applied ($dec FT8 decodes >= $FT8_FLOOR)"
else
  echo "  FAIL: type_err=$te (want 1), key/expected/got match=$triple (want >=1), decodes=$dec (want >= $FT8_FLOOR)"
  fail=1
fi

# --- Gate 15: Phase 6 CORRECTNESS GUARD — multithreaded_ft8 ignore-non-false ---
echo ""
echo "Gate 15 — multithreaded_ft8=true still decodes (apply ignores non-false)"
# lmultift8=.true. takes the decoder.f90:192 multithread branch, which yields 0
# FT8 decodes on the stream path. The apply IGNORES a non-false
# multithreaded_ft8, so FT8 must STILL decode >= floor. A straight
# pass-through regression would collapse FT8 to 0 and fail here. Compare against a
# fresh baseline to keep the assertion meaningful (both runs healthy).
base=$(python3 "$HARNESS" "$JT9" "$WAV" --mode FT8 --quiet 2>/dev/null \
       | grep -c '"t":"decode"' || true)
mt=$(python3 "$HARNESS" "$JT9" "$WAV" --mode FT8 --multithreaded-ft8 --quiet 2>/dev/null \
     | grep -c '"t":"decode"' || true)
if [[ "$base" -ge "$FT8_FLOOR" && "$mt" -ge "$FT8_FLOOR" ]]; then
  echo "  PASS: multithreaded_ft8=true ignored; FT8 still decodes ($mt >= $FT8_FLOOR, baseline $base)"
else
  echo "  FAIL: guard broken — multithreaded_ft8=true collapsed FT8 (with-key=$mt, baseline=$base, floor=$FT8_FLOOR)"
  fail=1
fi

# --- Gate 16: Phase 7 — all 6 keys accepted (parse + apply run clean) ---------
echo ""
echo "Gate 16 — Phase 7 diagnostic/sequencing keys accepted (6 keys: 4 ints + 2 bools)"
# All 6 keys present with correct types and benign values. All are inert on the
# FT8 fixture (multithread-block-only / AP passes with no AP-only decodes).
# qso_progress_state=5 reaches the single-pass FT8 decoder but
# only steers AP passes (no AP-only decodes on this fixture). Assertion: ZERO
# type errors (every key accepted) AND FT8 still decodes >= floor (apply ran
# without corrupting params). Per-key routing is guarded by
# run-phase7-parse-unit.sh + run-apply-routing-unit.sh.
out=$(python3 "$HARNESS" "$JT9" "$WAV" --mode FT8 \
      --last-tx-seconds-ago 3 --currently-txing --mode-changed \
      --qso-progress-state 5 --sec-band-changed 7 --delay-units 6 \
      --quiet 2>/dev/null || true)
te=$(printf '%s\n' "$out" | grep -c '"code":"configure_type_error"' || true)
dec=$(printf '%s\n' "$out" | grep -c '"t":"decode"' || true)
if [[ "$te" -eq 0 && "$dec" -ge "$FT8_FLOOR" ]]; then
  echo "  PASS: all 6 Phase 7 keys accepted (0 type errors); $dec decodes >= $FT8_FLOOR (apply clean)"
else
  echo "  FAIL: type_err=$te (want 0), decodes=$dec (want >= $FT8_FLOOR)"
  fail=1
fi

# --- Gate 17: Phase 7 — wrong-typed key -> type error + atomic decline --------
echo ""
echo "Gate 17 — Phase 7 wrong-typed key (last_tx_seconds_ago as string) declines frame"
# MAIN configure carries last_tx_seconds_ago:"soon" (string, wrong type for an
# int) AND sets mode=JT65. Atomic decline: the entire frame -> mode JT65 must
# NOT apply, so the FT8 audio decodes under the FT8 init default (>= floor). The
# error must name the Phase 7 key with expected:"int" got:"string".
out=$(python3 "$HARNESS" "$JT9" "$WAV" --mode JT65 --bad-phase7 --quiet 2>/dev/null || true)
te=$(printf '%s\n' "$out" | grep -c '"code":"configure_type_error"' || true)
triple=$(printf '%s\n' "$out" | grep -c '"key":"last_tx_seconds_ago","expected":"int","got":"string"' || true)
dec=$(printf '%s\n' "$out" | grep -c '"t":"decode"' || true)
if [[ "$te" -eq 1 && "$triple" -ge 1 && "$dec" -ge "$FT8_FLOOR" ]]; then
  echo "  PASS: one configure_type_error (last_tx_seconds_ago/int/string); frame declined, mode JT65 not applied ($dec FT8 decodes >= $FT8_FLOOR)"
else
  echo "  FAIL: type_err=$te (want 1), key/expected/got match=$triple (want >=1), decodes=$dec (want >= $FT8_FLOOR)"
  fail=1
fi

# --- Gate 18: Phase 7 CORRECTNESS GUARD — qso_progress_state out-of-range ------
echo ""
echo "Gate 18 — qso_progress_state=99 (out of range) still decodes (apply ignores it)"
# nQSOProgress reaches the single-pass FT8 decoder (decoder.f90:1141), where
# ft8b.f90:274/299 use it as a raw index into nappasses(0:5)/naptypes(0:5,4) on the
# default lft8apon=.true. path. An out-of-range value faults the decoder under
# -fbounds-check (qso_progress_state=99 crashes jt9 --stream). The
# apply IGNORES an out-of-range value (valid [0,5] only), so FT8 must STILL decode
# >= floor. A straight-pass-through regression would crash and collapse FT8 to 0.
# Both-sides-healthy: baseline >= floor AND with-key >= floor.
base=$(python3 "$HARNESS" "$JT9" "$WAV" --mode FT8 --quiet 2>/dev/null \
       | grep -c '"t":"decode"' || true)
oor=$(python3 "$HARNESS" "$JT9" "$WAV" --mode FT8 --qso-progress-state 99 --quiet 2>/dev/null \
      | grep -c '"t":"decode"' || true)
if [[ "$base" -ge "$FT8_FLOOR" && "$oor" -ge "$FT8_FLOOR" ]]; then
  echo "  PASS: qso_progress_state=99 ignored; FT8 still decodes ($oor >= $FT8_FLOOR, baseline $base)"
else
  echo "  FAIL: guard broken — out-of-range qso_progress_state collapsed FT8 (with-key=$oor, baseline=$base, floor=$FT8_FLOOR)"
  fail=1
fi

# --- Gate 19: Phase 8 — all 5 keys accepted + utc DECODE-OBSERVABLE -----------
echo ""
echo "Gate 19 — Phase 8 string/scaled/derived keys accepted; utc reflected in time"
# All 5 keys present with benign values. utc:"13:34:30" -> nutc 133430 is
# DECODE-OBSERVABLE in the decode "time" field (unlike Phases 4/6/7's inert
# fields, this is a real apply-routing guard, like the Gate 5 nutc pattern).
# date/n_trials/candthin_threshold/dt_center_seconds are inert on FT8 (superfox /
# JT65 / multithread-variant only), and the n_trials/candthin/
# dtcenter values equal the init defaults (nranera=6 / ncandthin=100 / ndtcenter=0)
# so they cannot perturb the count. Assertion: ZERO type errors, FT8 >= floor, AND
# every decode carries time 133430 (proves utc parsed -> nutc -> emitted).
out=$(python3 "$HARNESS" "$JT9" "$WAV" --mode FT8 \
      --utc "13:34:30" --date "2021-07-03" --n-trials 1000 \
      --candthin-threshold 1.0 --dt-center-seconds 0.0 \
      --quiet 2>/dev/null || true)
te=$(printf '%s\n' "$out" | grep -c '"code":"configure_type_error"' || true)
dec=$(printf '%s\n' "$out" | grep -c '"t":"decode"' || true)
rec=$(printf '%s\n' "$out" | grep '"t":"decode"' | grep -c '"time":"133430"' || true)
if [[ "$te" -eq 0 && "$dec" -ge "$FT8_FLOOR" && "$rec" -ge 1 && "$rec" -eq "$dec" ]]; then
  echo "  PASS: all 5 Phase 8 keys accepted (0 type errors); $dec decodes >= $FT8_FLOOR; utc reflected ($rec/$dec carry time 133430)"
else
  echo "  FAIL: type_err=$te (want 0), decodes=$dec (want >= $FT8_FLOOR), utc-reflected=$rec (want == $dec, >=1)"
  fail=1
fi

# --- Gate 20: Phase 8 — utc WINS over the legacy int nutc (precedence) --------
echo ""
echo "Gate 20 — utc precedence: utc beats a co-present nutc"
# A frame carrying BOTH nutc:120000 (legacy int) AND utc:"13:34:30" (ISO) must
# apply utc (133430), not nutc (120000). apply_configure_fields applies nutc then
# utc, so utc wins. Assertion: time 133430 present AND time 120000 ABSENT.
out=$(python3 "$HARNESS" "$JT9" "$WAV" --mode FT8 --nutc 120000 --utc "13:34:30" \
      --quiet 2>/dev/null || true)
dec=$(printf '%s\n' "$out" | grep -c '"t":"decode"' || true)
win=$(printf '%s\n' "$out" | grep '"t":"decode"' | grep -c '"time":"133430"' || true)
lose=$(printf '%s\n' "$out" | grep '"t":"decode"' | grep -c '"time":"120000"' || true)
if [[ "$dec" -ge "$FT8_FLOOR" && "$win" -ge 1 && "$lose" -eq 0 ]]; then
  echo "  PASS: utc wins ($win/$dec carry time 133430; 0 carry the nutc 120000)"
else
  echo "  FAIL: decodes=$dec (want >= $FT8_FLOOR), utc-wins=$win (want >=1), nutc-leak=$lose (want 0)"
  fail=1
fi

# --- Gate 21: Phase 8 — n_trials out of {10^N,3*10^N} -> type error + decline -
echo ""
echo "Gate 21 — n_trials=500 (invalid value) declines the frame"
# n_trials is type-correct (a number) but VALUE-invalid (not 10^N or 3*10^N), so the
# receiver emits configure_type_error (a bad nranera would silently corrupt ntrials,
# decoder.f90:143-145). The frame also sets mode=JT65; atomic decline of the WHOLE
# frame -> JT65 must NOT apply, so the FT8 audio decodes under the FT8 init default.
out=$(python3 "$HARNESS" "$JT9" "$WAV" --mode JT65 --n-trials 500 --quiet 2>/dev/null || true)
te=$(printf '%s\n' "$out" | grep -c '"code":"configure_type_error"' || true)
triple=$(printf '%s\n' "$out" | grep -F '"key":"n_trials","expected":"int 10^N|3*10^N","got":"number"' | grep -c . || true)
dec=$(printf '%s\n' "$out" | grep -c '"t":"decode"' || true)
if [[ "$te" -eq 1 && "$triple" -ge 1 && "$dec" -ge "$FT8_FLOOR" ]]; then
  echo "  PASS: one configure_type_error (n_trials value-invalid); frame declined, mode JT65 not applied ($dec FT8 decodes >= $FT8_FLOOR)"
else
  echo "  FAIL: type_err=$te (want 1), n_trials triple=$triple (want >=1), decodes=$dec (want >= $FT8_FLOOR)"
  fail=1
fi

# --- Gate 22: Phase 8 — date year >= 2100 -> type error + decline -------------
echo ""
echo "Gate 22 — date=2200-01-01 (year >= 2100) declines the frame"
# The 2000+offset YYMMDD encoding caps at 2099; a year >= 2100 is a
# configure_type_error. The frame also sets mode=JT65, which must NOT apply
# (whole-frame decline) -> FT8 decodes under its init default.
out=$(python3 "$HARNESS" "$JT9" "$WAV" --mode JT65 --date "2200-01-01" --quiet 2>/dev/null || true)
te=$(printf '%s\n' "$out" | grep -c '"code":"configure_type_error"' || true)
triple=$(printf '%s\n' "$out" | grep -F '"key":"date","expected":"year<2100","got":"string"' | grep -c . || true)
dec=$(printf '%s\n' "$out" | grep -c '"t":"decode"' || true)
if [[ "$te" -eq 1 && "$triple" -ge 1 && "$dec" -ge "$FT8_FLOOR" ]]; then
  echo "  PASS: one configure_type_error (date year>=2100); frame declined, mode JT65 not applied ($dec FT8 decodes >= $FT8_FLOOR)"
else
  echo "  FAIL: type_err=$te (want 1), date triple=$triple (want >=1), decodes=$dec (want >= $FT8_FLOOR)"
  fail=1
fi

# --- Gate 23: Phase 9 — all 9 unpacked keys accepted (parse + pack + apply clean)
echo ""
echo "Gate 23 — Phase 9 bit-packed keys accepted (9 keys: ndepth group + nexp_decode group)"
# All 9 unpacked keys present. depth_level=3 / q65_maxiters_level=3 keep ndepth=3
# (== the harness default depth=3), so the count is unperturbed; q65_maxiters agrees
# with depth_level's low 2 bits (consistency OK). use_averaging/deep_ap_search/
# q65_auto_clear_average (ndepth bits 4/5/7) + single_decode/vhf_features/
# noise_blanker (nexp_decode) are all inert on FT8 (Q65/JT65/FST4/JT4 only).
# contest_type=na_vhf -> ncontest=1 reaches FT8 ft8apset but the
# fixture has no contest/AP-only decodes. Assertion: ZERO type errors AND FT8 still
# decodes >= floor (parse->pack->apply ran clean). Per-key bit-packing is guarded by
# run-phase9-parse-unit.sh; per-key routing by run-apply-routing-unit.sh.
out=$(python3 "$HARNESS" "$JT9" "$WAV" --mode FT8 \
      --depth-level 3 --q65-maxiters-level 3 --use-averaging --deep-ap-search \
      --q65-auto-clear-average --contest-type na_vhf --single-decode \
      --vhf-features --noise-blanker-level 2 --quiet 2>/dev/null || true)
te=$(printf '%s\n' "$out" | grep -c '"code":"configure_type_error"' || true)
dec=$(printf '%s\n' "$out" | grep -c '"t":"decode"' || true)
if [[ "$te" -eq 0 && "$dec" -ge "$FT8_FLOOR" ]]; then
  echo "  PASS: all 9 Phase 9 keys accepted (0 type errors); $dec decodes >= $FT8_FLOOR (pack+apply clean)"
else
  echo "  FAIL: type_err=$te (want 0), decodes=$dec (want >= $FT8_FLOOR)"
  fail=1
fi

# --- Gate 24: Phase 9 APPLY ROUTING — depth_level is decode-observable ---------
echo ""
echo "Gate 24 — depth_level=1 changes the FT8 decode set (real apply-routing guard)"
# The FT8 fixture default runs at depth=3 -> ndepth=3 (21 decodes). Sending
# depth_level=1 (which auto-drops the legacy depth key) -> ndepth=1 takes the FT8
# fast-decode path (ft8_decode.f90:178 npass=2, :182 syncmin) -> FEWER decodes
# (14). A mis-route to an inert param would leave the count at 21. Assertion:
# baseline healthy (>= floor) AND with-key non-empty (>= 1, NOT a crash/shmem-0 —
# the both-sides-healthy guard: a 0 with-key also "differs" from a healthy
# baseline) AND with-key STRICTLY FEWER than baseline (the fast path
# decodes less). NB: the with-key bound is >= 1, NOT >= FT8_FLOOR, so the gate is
# robust to a future decode-core nudge in the fast-path count (which happens to be
# 14 == FT8_FLOOR today; coupling them would false-fail on a benign drift to 13).
# "< baseline" + ">= 1" still catches a mis-route (== baseline)
# and a crash (0).
base=$(python3 "$HARNESS" "$JT9" "$WAV" --mode FT8 --quiet 2>/dev/null \
       | grep -c '"t":"decode"' || true)
dl=$(python3 "$HARNESS" "$JT9" "$WAV" --mode FT8 --depth-level 1 --quiet 2>/dev/null \
     | grep -c '"t":"decode"' || true)
if [[ "$base" -ge "$FT8_FLOOR" && "$dl" -ge 1 && "$dl" -lt "$base" ]]; then
  echo "  PASS: depth_level=1 routed to ndepth (fewer decodes: $dl < baseline $base, both healthy)"
else
  echo "  FAIL: baseline=$base (want >= $FT8_FLOOR), depth_level=1=$dl (want >= 1 AND < $base)"
  fail=1
fi

# --- Gate 25: Phase 9 — depth + depth_level conflict -> type error + decline ---
echo ""
echo "Gate 25 — depth + depth_level co-present (conflict) declines the frame"
# A frame carrying BOTH the legacy depth (full int) and the unpacked depth_level is
# a conflicting-encodings error (the two are mutually exclusive). The
# parse post-pass pack_phase9_ emits configure_type_error key:"ndepth"
# expected:"depth|unpacked" got:"conflict". The frame also sets mode=JT65, which must
# NOT apply (whole-frame decline) -> FT8 decodes under its init default.
out=$(python3 "$HARNESS" "$JT9" "$WAV" --mode JT65 --phase9-conflict --quiet 2>/dev/null || true)
te=$(printf '%s\n' "$out" | grep -c '"code":"configure_type_error"' || true)
triple=$(printf '%s\n' "$out" | grep -F '"key":"ndepth","expected":"depth|unpacked","got":"conflict"' | grep -c . || true)
dec=$(printf '%s\n' "$out" | grep -c '"t":"decode"' || true)
if [[ "$te" -eq 1 && "$triple" -ge 1 && "$dec" -ge "$FT8_FLOOR" ]]; then
  echo "  PASS: one configure_type_error (ndepth conflict); frame declined, mode JT65 not applied ($dec FT8 decodes >= $FT8_FLOOR)"
else
  echo "  FAIL: type_err=$te (want 1), ndepth conflict triple=$triple (want >=1), decodes=$dec (want >= $FT8_FLOOR)"
  fail=1
fi

# --- Gate 26: Phase 9 — wrong-typed key -> type error + atomic decline ---------
echo ""
echo "Gate 26 — Phase 9 wrong-typed key (depth_level as string) declines frame"
# MAIN configure carries depth_level:"deep" (string, wrong type for an int) AND sets
# mode=JT65. Atomic decline: the entire frame -> mode JT65 must NOT apply, so the
# FT8 audio decodes under the FT8 init default (>= floor). The error must name the
# Phase 9 key with expected:"int" got:"string".
out=$(python3 "$HARNESS" "$JT9" "$WAV" --mode JT65 --bad-phase9 --quiet 2>/dev/null || true)
te=$(printf '%s\n' "$out" | grep -c '"code":"configure_type_error"' || true)
triple=$(printf '%s\n' "$out" | grep -c '"key":"depth_level","expected":"int","got":"string"' || true)
dec=$(printf '%s\n' "$out" | grep -c '"t":"decode"' || true)
if [[ "$te" -eq 1 && "$triple" -ge 1 && "$dec" -ge "$FT8_FLOOR" ]]; then
  echo "  PASS: one configure_type_error (depth_level/int/string); frame declined, mode JT65 not applied ($dec FT8 decodes >= $FT8_FLOOR)"
else
  echo "  FAIL: type_err=$te (want 1), key/expected/got match=$triple (want >=1), decodes=$dec (want >= $FT8_FLOOR)"
  fail=1
fi

echo ""
if [[ "$fail" -eq 0 ]]; then
  echo "All schema-v1 fixtures passed."
else
  echo "Some schema-v1 fixtures FAILED."
  exit 1
fi
